//! What crosses the wire between a game client and server (ADR-083). `wire-relay` sits between them like an observer
//! on the path, forwarding UDP both ways and recording every datagram; `wire-scan` then looks for given strings in
//! the recording. Unreal packs its packets bit by bit, so a string is searched at each of the eight bit offsets.
//!
//! Recording format: one datagram per line, `<c|s> <milliseconds since start> <hex payload>`; `c` came from the
//! client, `s` from the server.

use std::fs::File;
use std::io::{BufRead, BufReader, Write};
use std::net::{SocketAddr, UdpSocket};
use std::sync::{Arc, Mutex};
use std::time::Instant;

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn unhex(text: &str) -> Option<Vec<u8>> {
    if text.len() % 2 != 0 {
        return None;
    }
    (0..text.len()).step_by(2).map(|i| u8::from_str_radix(&text[i..i + 2], 16).ok()).collect()
}

/// `wire-relay <listen addr> <server addr> <record file>`: forwards one client's datagrams until killed.
pub fn relay(listen: &str, server: &str, record: &str) -> u8 {
    let run = || -> Result<(), String> {
        let server: SocketAddr = server.parse().map_err(|e| format!("server address {server}: {e}"))?;
        let near = UdpSocket::bind(listen).map_err(|e| format!("bind {listen}: {e}"))?;
        let far = UdpSocket::bind("127.0.0.1:0").map_err(|e| format!("bind upstream: {e}"))?;
        far.connect(server).map_err(|e| format!("connect {server}: {e}"))?;
        let file = Arc::new(Mutex::new(File::create(record).map_err(|e| format!("{record}: {e}"))?));
        let client: Arc<Mutex<Option<SocketAddr>>> = Arc::new(Mutex::new(None));
        let started = Instant::now();
        println!("mb2 wire-relay listening={listen} server={server} record={record}");
        let write = {
            let file = Arc::clone(&file);
            move |direction: char, bytes: &[u8]| {
                let mut file = file.lock().expect("record lock");
                let _ = writeln!(file, "{direction} {} {}", started.elapsed().as_millis(), hex(bytes));
                let _ = file.flush();
            }
        };
        let back = {
            let (near, far, client, write) = (near.try_clone().map_err(|e| e.to_string())?, far.try_clone().map_err(|e| e.to_string())?,
                Arc::clone(&client), write.clone());
            std::thread::spawn(move || {
                let mut buffer = [0u8; 65536];
                while let Ok(n) = far.recv(&mut buffer) {
                    write('s', &buffer[..n]);
                    if let Some(to) = *client.lock().expect("client lock") {
                        let _ = near.send_to(&buffer[..n], to);
                    }
                }
            })
        };
        let mut buffer = [0u8; 65536];
        loop {
            let (n, from) = near.recv_from(&mut buffer).map_err(|e| format!("receive: {e}"))?;
            client.lock().expect("client lock").get_or_insert(from);
            write('c', &buffer[..n]);
            let _ = far.send(&buffer[..n]);
            if back.is_finished() {
                return Err("the upstream socket closed".into());
            }
        }
    };
    match run() {
        Ok(()) => 0,
        Err(error) => {
            eprintln!("mb2 wire-relay: {error}");
            2
        }
    }
}

/// The payload shifted right by `shift` bits, reading Unreal's bit stream (least significant bit first).
fn shifted(payload: &[u8], shift: u32) -> Vec<u8> {
    if shift == 0 {
        return payload.to_vec();
    }
    (0..payload.len())
        .map(|i| (payload[i] >> shift) | payload.get(i + 1).map_or(0, |next| next << (8 - shift)))
        .collect()
}

/// Whether `needle` appears in `payload` at any bit offset.
pub fn contains_at_any_bit(payload: &[u8], needle: &[u8]) -> bool {
    !needle.is_empty() && (0..8).any(|shift| shifted(payload, shift).windows(needle.len()).any(|w| w == needle))
}

/// `wire-scan <record> <needle>...`: per needle, how many datagrams from each side carry it.
/// Prints `wire packets=<n> client=<n> server=<n>` and one `wire needle=<text> client_hits=<n> server_hits=<n>
/// first_ms=<ms|->` line per needle.
pub fn scan(record: &str, needles: &[&str]) -> u8 {
    let file = match File::open(record) {
        Ok(file) => file,
        Err(error) => {
            eprintln!("mb2 wire-scan: {record}: {error}");
            return 2;
        }
    };
    let mut packets: Vec<(char, u64, Vec<u8>)> = Vec::new();
    for line in BufReader::new(file).lines().map_while(Result::ok) {
        let mut parts = line.split(' ');
        let (Some(direction), Some(ms), Some(payload)) = (parts.next(), parts.next(), parts.next()) else { continue };
        let (Some(direction), Ok(ms), Some(payload)) = (direction.chars().next(), ms.parse(), unhex(payload)) else { continue };
        packets.push((direction, ms, payload));
    }
    let count = |side: char| packets.iter().filter(|(d, _, _)| *d == side).count();
    println!("wire packets={} client={} server={}", packets.len(), count('c'), count('s'));
    for needle in needles {
        let hits: Vec<&(char, u64, Vec<u8>)> = packets.iter().filter(|(_, _, p)| contains_at_any_bit(p, needle.as_bytes())).collect();
        let side = |s: char| hits.iter().filter(|(d, _, _)| *d == s).count();
        println!("wire needle={needle} client_hits={} server_hits={} first_ms={}", side('c'), side('s'),
            hits.first().map_or("-".to_string(), |(_, ms, _)| ms.to_string()));
    }
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A string written into a bit stream at an arbitrary bit offset, least significant bit first.
    fn at_bit(prefix_bits: u32, text: &[u8]) -> Vec<u8> {
        let mut bits: Vec<bool> = (0..prefix_bits).map(|i| i % 3 == 0).collect();
        for byte in text {
            bits.extend((0..8).map(|i| byte >> i & 1 == 1));
        }
        bits.chunks(8).map(|chunk| chunk.iter().enumerate().fold(0u8, |b, (i, &on)| b | (u8::from(on) << i))).collect()
    }

    #[test]
    fn strings_are_found_at_every_bit_offset() {
        for offset in 0..24 {
            assert!(contains_at_any_bit(&at_bit(offset, b"/Game/World/lent"), b"/Game/World/lent"), "offset {offset}");
        }
        assert!(!contains_at_any_bit(&at_bit(5, b"/Game/World/lent"), b"/Game/World/lens"));
        assert!(!contains_at_any_bit(b"abc", b""));
    }

    #[test]
    fn a_recording_round_trips() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("wire.txt");
        std::fs::write(&path, format!("c 1 {}\ns 2 {}\ns 3 00ff\n", hex(&at_bit(3, b"Mlakar")), hex(b"x"))).unwrap();
        assert_eq!(scan(path.to_str().unwrap(), &["Mlakar"]), 0);
        assert_eq!(unhex("00ff"), Some(vec![0, 255]));
        assert_eq!(unhex("0"), None);
    }
}
