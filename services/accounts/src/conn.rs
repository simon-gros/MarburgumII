//! Connection limits (QA 2026-11-19, AR4). Without them a client could open connections, finish the TLS
//! handshake and then send nothing: hyper's header-read timeout starts only once the protocol is known, and the
//! protocol detection in front of it waits for the first bytes with no deadline. A few thousand silent connections
//! used up the process's file descriptors, and the game servers' redeems could no longer connect.
//!
//! Every accepted socket now holds one of `MAX_CONNECTIONS` places, given back when it closes, and is closed when no
//! byte arrives for `IDLE_SECONDS`. The service's requests are a few hundred bytes, so a live client never waits
//! that long between bytes.

use axum_server::accept::Accept;
use std::future::{Future, Ready};
use std::io;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};
use std::time::Duration;
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};
use tokio::sync::{OwnedSemaphorePermit, Semaphore};
use tokio::time::{sleep, Instant, Sleep};

/// Open connections at once; the next is closed at accept. Below the usual soft descriptor limit of 1024, leaving
/// room for the store's files.
pub const MAX_CONNECTIONS: usize = 768;
/// Silence after which a connection is closed: no handshake byte, no request byte.
pub const IDLE_SECONDS: u64 = 30;

/// The acceptor in front of TLS: a place among `MAX_CONNECTIONS`, and the idle deadline.
#[derive(Clone)]
pub struct Limited {
    places: Arc<Semaphore>,
    idle: Duration,
}

impl Limited {
    pub fn new(max_connections: usize, idle: Duration) -> Self {
        Limited { places: Arc::new(Semaphore::new(max_connections)), idle }
    }
}

impl<I, S> Accept<I, S> for Limited
where
    I: AsyncRead + AsyncWrite + Unpin,
{
    type Stream = Idle<I>;
    type Service = S;
    type Future = Ready<io::Result<(Self::Stream, Self::Service)>>;

    fn accept(&self, stream: I, service: S) -> Self::Future {
        std::future::ready(match self.places.clone().try_acquire_owned() {
            Ok(place) => Ok((Idle::new(stream, self.idle, place), service)),
            Err(_) => Err(io::Error::other("connection limit reached")),
        })
    }
}

/// A stream that fails its read once nothing has arrived for the idle time.
pub struct Idle<I> {
    inner: I,
    idle: Duration,
    deadline: Pin<Box<Sleep>>,
    _place: OwnedSemaphorePermit,
}

impl<I> Idle<I> {
    fn new(inner: I, idle: Duration, place: OwnedSemaphorePermit) -> Self {
        Idle { inner, idle, deadline: Box::pin(sleep(idle)), _place: place }
    }
}

impl<I: AsyncRead + Unpin> AsyncRead for Idle<I> {
    fn poll_read(mut self: Pin<&mut Self>, cx: &mut Context<'_>, buf: &mut ReadBuf<'_>) -> Poll<io::Result<()>> {
        let this = &mut *self;
        let before = buf.filled().len();
        match Pin::new(&mut this.inner).poll_read(cx, buf) {
            Poll::Ready(Ok(())) => {
                if buf.filled().len() > before {
                    let next = Instant::now() + this.idle;
                    this.deadline.as_mut().reset(next);
                }
                Poll::Ready(Ok(()))
            }
            Poll::Ready(Err(error)) => Poll::Ready(Err(error)),
            Poll::Pending => match this.deadline.as_mut().poll(cx) {
                Poll::Ready(()) => Poll::Ready(Err(io::Error::new(io::ErrorKind::TimedOut, "connection idle"))),
                Poll::Pending => Poll::Pending,
            },
        }
    }
}

impl<I: AsyncWrite + Unpin> AsyncWrite for Idle<I> {
    fn poll_write(mut self: Pin<&mut Self>, cx: &mut Context<'_>, buf: &[u8]) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.inner).poll_write(cx, buf)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.inner).poll_flush(cx)
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.inner).poll_shutdown(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    #[tokio::test(start_paused = true)]
    async fn a_silent_connection_is_closed_and_a_talking_one_is_not() {
        let limited = Limited::new(4, Duration::from_secs(IDLE_SECONDS));
        let (client, server) = tokio::io::duplex(64);
        let (mut stream, ()) = limited.accept(server, ()).await.unwrap();
        let mut client = client;
        let mut byte = [0u8; 1];
        // Bytes every 20 s keep it open well past the idle time.
        for _ in 0..4 {
            tokio::time::advance(Duration::from_secs(20)).await;
            client.write_all(b"x").await.unwrap();
            stream.read_exact(&mut byte).await.unwrap();
        }
        // Then silence: the read fails once the idle time has passed, not before.
        let silent = tokio::spawn(async move { stream.read(&mut byte).await });
        tokio::time::advance(Duration::from_secs(IDLE_SECONDS - 1)).await;
        assert!(!silent.is_finished());
        tokio::time::advance(Duration::from_secs(2)).await;
        let error = silent.await.unwrap().unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::TimedOut);
    }

    #[tokio::test]
    async fn connections_past_the_limit_are_refused_and_a_closed_one_frees_its_place() {
        let limited = Limited::new(2, Duration::from_secs(IDLE_SECONDS));
        let (_a, a) = tokio::io::duplex(8);
        let (_b, b) = tokio::io::duplex(8);
        let (_c, c) = tokio::io::duplex(8);
        let first = limited.accept(a, ()).await.unwrap();
        let _second = limited.accept(b, ()).await.unwrap();
        assert!(limited.accept(c, ()).await.is_err(), "a third connection over a limit of two");
        drop(first);
        let (_d, d) = tokio::io::duplex(8);
        assert!(limited.accept(d, ()).await.is_ok(), "the first one's place is free again");
    }
}
