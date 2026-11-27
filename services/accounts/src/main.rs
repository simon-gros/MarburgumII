//! mb2-accounts: the Marburgum II account service (ADR-055).
//!
//! mb2-accounts --bind 127.0.0.1:7790 --store <accounts.json> --outbox <dir> --models <character_models.json>
//!              --servers <servers.json> --tls-cert <cert.pem> --tls-key <key.pem>
//!
//! The servers file lists the game servers tickets are issued for, each with its own key file (src/servers.rs).
//!
//! HTTPS only. Emails are written to the outbox folder, one file each, until a real sender is chosen.

mod conn;
mod http;
mod policy;
mod secrets;
mod servers;
mod service;
mod store;

use hyper_util::rt::TokioTimer;
use std::fs;
use std::io::Write;
use std::net::SocketAddr;
use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::{Arc, Mutex};
use std::time::Duration;

/// The development sender: each email is a file in a folder only the service's user can read.
struct Outbox(PathBuf);

impl service::Mailer for Outbox {
    fn send(&self, to: &str, subject: &str, body: &str) -> std::io::Result<()> {
        fs::create_dir_all(&self.0)?;
        let name = format!("{}-{}.txt", std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_millis(),
            &secrets::id("m_")[2..10]);
        let mut options = fs::OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        std::os::unix::fs::OpenOptionsExt::mode(&mut options, 0o600);
        let mut file = options.open(self.0.join(name))?;
        write!(file, "To: {to}\nSubject: {subject}\n\n{body}")?;
        file.sync_all()
    }
}

struct Args {
    bind: SocketAddr,
    store: PathBuf,
    outbox: PathBuf,
    models: PathBuf,
    servers: PathBuf,
    tls_cert: PathBuf,
    tls_key: PathBuf,
}

fn parse() -> Result<Args, String> {
    let mut values = std::collections::HashMap::new();
    let mut it = std::env::args().skip(1);
    while let Some(flag) = it.next() {
        let value = it.next().ok_or(format!("{flag} needs a value"))?;
        values.insert(flag.trim_start_matches("--").to_string(), value);
    }
    let mut take = |name: &str| values.remove(name).ok_or(format!("missing --{name}"));
    Ok(Args {
        bind: take("bind")?.parse().map_err(|e| format!("--bind: {e}"))?,
        store: take("store")?.into(),
        outbox: take("outbox")?.into(),
        models: take("models")?.into(),
        servers: take("servers")?.into(),
        tls_cert: take("tls-cert")?.into(),
        tls_key: take("tls-key")?.into(),
    })
}

/// The ids of the launch character models, from the same catalogue the game reads.
fn model_ids(path: &PathBuf) -> Result<Vec<String>, String> {
    let doc: serde_json::Value = serde_json::from_slice(&fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?)
        .map_err(|e| format!("{}: {e}", path.display()))?;
    let ids: Vec<String> = doc["models"].as_array().into_iter().flatten().filter_map(|m| m["id"].as_str().map(String::from)).collect();
    if ids.is_empty() {
        return Err(format!("{}: no models", path.display()));
    }
    Ok(ids)
}

#[tokio::main]
async fn main() -> ExitCode {
    let args = match parse() {
        Ok(args) => args,
        Err(error) => {
            eprintln!("mb2-accounts: {error}");
            return ExitCode::from(2);
        }
    };
    let setup = || -> Result<service::Service, String> {
        let models = model_ids(&args.models)?;
        let servers = servers::load(&args.servers)?;
        for server in &servers {
            println!("mb2-accounts event=game_server id={} addresses={}", server.id, server.addresses.join(","));
        }
        service::Service::open(args.store.clone(), Box::new(Outbox(args.outbox.clone())), models, servers)
    };
    let service = match setup() {
        Ok(service) => service,
        Err(error) => {
            eprintln!("mb2-accounts: {error}");
            return ExitCode::from(1);
        }
    };
    let tls = match axum_server::tls_rustls::RustlsConfig::from_pem_file(&args.tls_cert, &args.tls_key).await {
        Ok(tls) => tls,
        Err(error) => {
            eprintln!("mb2-accounts: TLS: {error}");
            return ExitCode::from(1);
        }
    };
    let revision = service.store().revision;
    let app = http::router(Arc::new(Mutex::new(service)));
    let handle = axum_server::Handle::new();
    let stopping = handle.clone();
    tokio::spawn(async move {
        terminated().await;
        stopping.graceful_shutdown(Some(std::time::Duration::from_secs(5)));
    });
    println!("mb2-accounts event=listening bind={} store_revision={revision}", args.bind);
    // A place among conn::MAX_CONNECTIONS and the idle deadline, under TLS (AR4); HTTP/1 headers must then arrive
    // within ten seconds, and HTTP/2 connections that stop answering pings are closed.
    let limited = conn::Limited::new(conn::MAX_CONNECTIONS, Duration::from_secs(conn::IDLE_SECONDS));
    let mut server = axum_server::bind(args.bind).acceptor(axum_server::tls_rustls::RustlsAcceptor::new(tls).acceptor(limited)).handle(handle);
    server.http_builder().http1().timer(TokioTimer::new()).header_read_timeout(Duration::from_secs(10));
    server.http_builder().http2().timer(TokioTimer::new()).keep_alive_interval(Duration::from_secs(20)).keep_alive_timeout(Duration::from_secs(10));
    // The peer address feeds the per-address limit on the public routes.
    match server.serve(app.into_make_service_with_connect_info::<SocketAddr>()).await {
        Ok(()) => {
            println!("mb2-accounts event=stopped");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("mb2-accounts: {error}");
            ExitCode::from(1)
        }
    }
}

/// Ctrl+C, or the service manager's stop request: SIGTERM on Unix, CTRL_BREAK/CTRL_CLOSE/CTRL_SHUTDOWN on Windows.
#[cfg(unix)]
async fn terminated() {
    let mut term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()).expect("SIGTERM");
    tokio::select! {
        _ = tokio::signal::ctrl_c() => {}
        _ = term.recv() => {}
    }
}

#[cfg(windows)]
async fn terminated() {
    let mut brk = tokio::signal::windows::ctrl_break().expect("CTRL_BREAK");
    let mut close = tokio::signal::windows::ctrl_close().expect("CTRL_CLOSE");
    let mut shutdown = tokio::signal::windows::ctrl_shutdown().expect("CTRL_SHUTDOWN");
    tokio::select! {
        _ = tokio::signal::ctrl_c() => {}
        _ = brk.recv() => {}
        _ = close.recv() => {}
        _ = shutdown.recv() => {}
    }
}
