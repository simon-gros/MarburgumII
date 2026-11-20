//! The HTTPS interface (ADR-055). JSON in and out; a refusal is `{"error": "<code>"}` with its status, a malformed
//! request included. A player's session travels as `Authorization: Bearer`, the game server's key as
//! `Authorization: MB2-Server`. Nothing secret is logged.

use crate::service::{self, Outcome, Refusal, Service};
use axum::extract::{ConnectInfo, DefaultBodyLimit, Extension, FromRequest, Request, State};
use axum::http::{HeaderMap, StatusCode};
use axum::middleware::{self, Next};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::de::DeserializeOwned;
use serde::Deserialize;
use serde_json::json;
use std::collections::HashMap;
use std::net::{IpAddr, Ipv6Addr, SocketAddr};
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::sync::{OwnedSemaphorePermit, Semaphore};

pub type Shared = Arc<Mutex<Service>>;

/// The largest request body: every request is a few short fields.
pub const BODY_LIMIT_BYTES: usize = 16 * 1024;
/// Unauthenticated requests (register, verify, setup, login, forgot, reset) one address may make a minute. One
/// client cannot then flood the service with password checks, mail or new store entries.
pub const PUBLIC_REQUESTS_PER_MINUTE: u32 = 60;

/// A JSON body whose rejection (bad JSON, a missing field, the wrong content type, too large) answers
/// `{"error": "request_invalid"}` like every other refusal, instead of axum's plain text.
pub struct JsonBody<T>(pub T);

impl<S: Send + Sync, T: DeserializeOwned> FromRequest<S> for JsonBody<T> {
    type Rejection = Response;

    async fn from_request(request: Request, state: &S) -> Result<Self, Self::Rejection> {
        match Json::<T>::from_request(request, state).await {
            Ok(Json(value)) => Ok(JsonBody(value)),
            Err(_) => Err((StatusCode::BAD_REQUEST, Json(json!({ "error": "request_invalid" }))).into_response()),
        }
    }
}

/// The address a limit counts: IPv4 as it is, IPv6 by its /64, which one host or home network is given whole.
/// Counted per /128, one machine had 2^64 separate allowances (QA 2026-11-19, AR5).
pub fn limit_bucket(address: IpAddr) -> IpAddr {
    match address {
        IpAddr::V4(v4) => IpAddr::V4(v4),
        IpAddr::V6(v6) => match v6.to_ipv4_mapped() {
            Some(v4) => IpAddr::V4(v4),
            None => IpAddr::V6(Ipv6Addr::from(u128::from(v6) & !((1u128 << 64) - 1))),
        },
    }
}

/// Password hashing (Argon2id, 19 MiB and some 25 ms each) runs at most `parallel` at a time on the blocking pool,
/// with a short queue behind; more waiting than that answers 503 `service_busy` at once. Unbounded, a burst ran
/// hundreds of checks together (gigabytes of memory) and filled the pool the game server's redeem needs (QA
/// 2026-11-19, AR2).
pub struct HashGate {
    running: Semaphore,
    admitted: Arc<Semaphore>,
}

/// Checks admitted per running slot, the running one included.
pub const HASH_QUEUE_PER_SLOT: usize = 5;

impl HashGate {
    pub fn new(parallel: usize) -> Self {
        let parallel = parallel.max(1);
        HashGate { running: Semaphore::new(parallel), admitted: Arc::new(Semaphore::new(parallel * HASH_QUEUE_PER_SLOT)) }
    }

    /// Half the cores, one to four: the locked rules keep cores of their own.
    pub fn for_host() -> Self {
        let cores = std::thread::available_parallelism().map(usize::from).unwrap_or(2);
        Self::new((cores / 2).clamp(1, 4))
    }

    /// A place in the queue, or `service_busy` when it is full.
    pub fn admit(&self) -> Outcome<OwnedSemaphorePermit> {
        self.admitted.clone().try_acquire_owned().map_err(|_| Refusal { status: 503, code: "service_busy" })
    }

    /// Runs `work` once a slot is free; `place` is given up when it is done.
    pub async fn run<T: Send + 'static>(&self, place: OwnedSemaphorePermit, work: impl FnOnce() -> T + Send + 'static) -> Outcome<T> {
        let _place = place;
        let _slot = self.running.acquire().await.map_err(|_| Refusal { status: 503, code: "service_busy" })?;
        tokio::task::spawn_blocking(work).await.map_err(|_| Refusal { status: 503, code: "service_busy" })
    }
}

/// Requests per address in the current minute, for the public routes.
#[derive(Default)]
pub struct PublicLimit {
    minute: i64,
    counts: HashMap<IpAddr, u32>,
}

impl PublicLimit {
    /// Counts one request from `address`; false once it is over the limit for this minute.
    pub fn admit(&mut self, address: IpAddr, now: i64) -> bool {
        if now / 60 != self.minute {
            self.minute = now / 60;
            self.counts.clear();
        }
        let count = self.counts.entry(limit_bucket(address)).or_insert(0);
        *count += 1;
        *count <= PUBLIC_REQUESTS_PER_MINUTE
    }
}

async fn limit_public(State(limit): State<Arc<Mutex<PublicLimit>>>, request: Request, next: Next) -> Response {
    // Behind a reverse proxy every request has the proxy's address; this limit is for direct connections.
    if let Some(ConnectInfo(peer)) = request.extensions().get::<ConnectInfo<SocketAddr>>() {
        let admitted = limit.lock().unwrap_or_else(|poisoned| poisoned.into_inner()).admit(peer.ip(), now());
        if !admitted {
            return (StatusCode::TOO_MANY_REQUESTS, Json(json!({ "error": "too_many_requests" }))).into_response();
        }
    }
    next.run(request).await
}

/// Seconds since the epoch; tests drive the service with their own clock instead.
fn now() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0)
}

fn answer<T: serde::Serialize>(outcome: Outcome<T>, ok: StatusCode) -> Response {
    match outcome {
        Ok(value) => (ok, Json(value)).into_response(),
        Err(refusal) => {
            let status = StatusCode::from_u16(refusal.status).unwrap_or(StatusCode::BAD_REQUEST);
            (status, Json(json!({ "error": refusal.code }))).into_response()
        }
    }
}

fn credential<'a>(headers: &'a HeaderMap, scheme: &str) -> &'a str {
    headers.get("authorization").and_then(|v| v.to_str().ok()).and_then(|v| v.strip_prefix(scheme)).map(str::trim).unwrap_or("")
}

/// Runs a rule off the async threads: rules write the store and some hash a password.
async fn run<T: Send + 'static>(shared: &Shared, rule: impl FnOnce(&mut Service, i64) -> T + Send + 'static) -> T {
    let shared = shared.clone();
    tokio::task::spawn_blocking(move || {
        let mut service = shared.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        rule(&mut service, now())
    })
    .await
    .expect("account rule panicked")
}

/// Sign-in in three steps; the Argon2 check in the middle runs without the service's lock, through the gate. A full
/// gate refuses before the attempt counts against the username.
async fn login(shared: &Shared, gate: &HashGate, username: String, password: String) -> Outcome<service::SignedIn> {
    let place = gate.admit()?;
    let check = run(shared, move |svc, now| svc.login_begin(&username, now)).await?;
    let phc = check.phc().map(str::to_string);
    let accepted = gate.run(place, move || service::check_password(&password, phc.as_deref())).await.unwrap_or(false);
    run(shared, move |svc, now| svc.login_finish(check, accepted, now)).await
}

/// Account setup: checked under the lock, hashed outside it, checked again and committed (AR8).
async fn setup(shared: &Shared, gate: &HashGate, body: SetupBody) -> Outcome<service::AccountView> {
    let place = gate.admit()?;
    let body = Arc::new(body);
    let checked = body.clone();
    run(shared, move |svc, now| svc.setup_check(&checked.ticket, &checked.username, &checked.password, now).map(|_| ())).await?;
    let hashed = body.clone();
    let phc = gate.run(place, move || crate::secrets::hash_password(&hashed.password)).await?;
    run(shared, move |svc, now| svc.setup_commit(&body.ticket, &body.username, &body.password, phc, now)).await
}

/// Password reset: the code checked under the lock, the new password hashed outside it, then committed (AR8).
async fn reset(shared: &Shared, gate: &HashGate, body: ResetBody) -> Outcome<()> {
    let place = gate.admit()?;
    let ResetBody { email, code, password } = body;
    let checked = password.clone();
    let check = run(shared, move |svc, now| svc.reset_begin(&email, &code, &checked, now)).await?;
    let phc = gate.run(place, move || crate::secrets::hash_password(&password)).await?;
    run(shared, move |svc, now| svc.reset_finish(check, phc, now)).await
}

/// "Forgot password" answers before it looks the address up, so neither its timing nor a failed write or mail can
/// tell a stranger whether the address has an account (QA 2026-11-19, AR6). Failures are logged, without the address.
fn forgot(shared: &Shared, email: String) {
    let shared = shared.clone();
    tokio::spawn(async move {
        if let Err(refusal) = run(&shared, move |svc, now| svc.forgot(&email, now)).await {
            eprintln!("mb2-accounts event=forgot_failed code={}", refusal.code);
        }
    });
}

#[derive(Deserialize)]
struct EmailBody {
    email: String,
}

#[derive(Deserialize)]
struct VerifyBody {
    email: String,
    code: String,
}

#[derive(Deserialize)]
struct SetupBody {
    ticket: String,
    username: String,
    password: String,
}

#[derive(Deserialize)]
struct LoginBody {
    username: String,
    password: String,
}

#[derive(Deserialize)]
struct CharacterBody {
    request: String,
    first_name: String,
    last_name: String,
    model: String,
}

#[derive(Deserialize)]
struct ResetBody {
    email: String,
    code: String,
    password: String,
}

/// The game server address the client is about to join (AR1).
#[derive(Deserialize)]
struct AdmissionBody {
    server: String,
}

#[derive(Deserialize)]
struct RedeemBody {
    ticket: String,
}

pub fn router(shared: Shared) -> Router {
    router_with(shared, HashGate::for_host())
}

async fn not_found() -> Response {
    (StatusCode::NOT_FOUND, Json(json!({ "error": "not_found" }))).into_response()
}

async fn method_not_allowed() -> Response {
    (StatusCode::METHOD_NOT_ALLOWED, Json(json!({ "error": "method_not_allowed" }))).into_response()
}

pub fn router_with(shared: Shared, gate: HashGate) -> Router {
    let gate = Arc::new(gate);
    let public = Router::new()
        .route("/v1/register", post(|State(s): State<Shared>, JsonBody(b): JsonBody<EmailBody>| async move {
            answer(run(&s, move |svc, now| svc.register(&b.email, now).map(|_| json!({ "sent": true }))).await, StatusCode::ACCEPTED)
        }))
        .route("/v1/verify", post(|State(s): State<Shared>, JsonBody(b): JsonBody<VerifyBody>| async move {
            answer(run(&s, move |svc, now| svc.verify(&b.email, &b.code, now)).await, StatusCode::OK)
        }))
        .route("/v1/setup", post(|State(s): State<Shared>, Extension(g): Extension<Arc<HashGate>>, JsonBody(b): JsonBody<SetupBody>| async move {
            answer(setup(&s, &g, b).await, StatusCode::CREATED)
        }))
        .route("/v1/login", post(|State(s): State<Shared>, Extension(g): Extension<Arc<HashGate>>, JsonBody(b): JsonBody<LoginBody>| async move {
            answer(login(&s, &g, b.username, b.password).await, StatusCode::OK)
        }))
        .route("/v1/forgot", post(|State(s): State<Shared>, JsonBody(b): JsonBody<EmailBody>| async move {
            forgot(&s, b.email);
            (StatusCode::ACCEPTED, Json(json!({ "sent": true })))
        }))
        .route("/v1/reset", post(|State(s): State<Shared>, Extension(g): Extension<Arc<HashGate>>, JsonBody(b): JsonBody<ResetBody>| async move {
            answer(reset(&s, &g, b).await.map(|_| json!({})), StatusCode::OK)
        }))
        .route_layer(middleware::from_fn_with_state(Arc::new(Mutex::new(PublicLimit::default())), limit_public));
    Router::new()
        .merge(public)
        .route("/v1/health", get(|| async { Json(json!({ "service": "mb2-accounts", "api": 1 })) }))
        .route("/v1/me", get(|State(s): State<Shared>, h: HeaderMap| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.me(&token, now)).await, StatusCode::OK)
        }))
        .route("/v1/logout", post(|State(s): State<Shared>, h: HeaderMap| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.logout(&token, now).map(|_| json!({}))).await, StatusCode::OK)
        }))
        .route("/v1/character", post(|State(s): State<Shared>, h: HeaderMap, JsonBody(b): JsonBody<CharacterBody>| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.create_character(&token, &b.request, &b.first_name, &b.last_name, &b.model, now)).await,
                StatusCode::OK)
        }))
        .route("/v1/admission", post(|State(s): State<Shared>, h: HeaderMap, JsonBody(b): JsonBody<AdmissionBody>| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.admission(&token, &b.server, now)).await, StatusCode::OK)
        }))
        .route("/v1/server/redeem", post(|State(s): State<Shared>, h: HeaderMap, JsonBody(b): JsonBody<RedeemBody>| async move {
            let key = credential(&h, "MB2-Server ").to_string();
            answer(run(&s, move |svc, now| svc.redeem(&key, &b.ticket, now)).await, StatusCode::OK)
        }))
        // Unknown routes and methods answer in the error format too (AR12).
        .fallback(not_found)
        .method_not_allowed_fallback(method_not_allowed)
        .layer(DefaultBodyLimit::max(BODY_LIMIT_BYTES))
        .layer(Extension(gate))
        .with_state(shared)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::service::tests::{service, KEY, OTHER_KEY, SERVER};
    use axum::body::Body;
    use axum::http::Request;
    use http_body_util::BodyExt;
    use tower::ServiceExt;

    async fn call(app: &Router, method: &str, path: &str, auth: Option<&str>, body: serde_json::Value) -> (StatusCode, serde_json::Value) {
        let mut request = Request::builder().method(method).uri(path).header("content-type", "application/json");
        if let Some(auth) = auth {
            request = request.header("authorization", auth);
        }
        let response = app.clone().oneshot(request.body(Body::from(body.to_string())).unwrap()).await.unwrap();
        let status = response.status();
        let bytes = response.into_body().collect().await.unwrap().to_bytes();
        (status, serde_json::from_slice(&bytes).unwrap_or(serde_json::Value::Null))
    }

    async fn send(app: &Router, request: Request<Body>) -> (StatusCode, serde_json::Value) {
        let response = app.clone().oneshot(request).await.unwrap();
        let status = response.status();
        let bytes = response.into_body().collect().await.unwrap().to_bytes();
        (status, serde_json::from_slice(&bytes).unwrap_or(serde_json::Value::Null))
    }

    #[tokio::test]
    async fn malformed_requests_answer_in_the_error_format() {
        let dir = tempfile::tempdir().unwrap();
        let (svc, _) = service(dir.path());
        let app = router(Arc::new(Mutex::new(svc)));
        let (status, body) = call(&app, "POST", "/v1/login", None, json!({})).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::BAD_REQUEST, Some("request_invalid")), "missing fields");
        let wrong_type = Request::builder().method("POST").uri("/v1/login").header("content-type", "text/plain")
            .body(Body::from(r#"{"username":"ana_k","password":"x"}"#)).unwrap();
        let (status, body) = send(&app, wrong_type).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::BAD_REQUEST, Some("request_invalid")), "content type");
        let huge = json!({ "username": "x".repeat(BODY_LIMIT_BYTES), "password": "drava-bridge-1990" });
        let (status, body) = call(&app, "POST", "/v1/login", None, huge).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::BAD_REQUEST, Some("request_invalid")), "too large");
        let (status, body) = call(&app, "POST", "/v1/server/redeem", Some(&format!("MB2-Server {KEY}")), json!({ "ticket": 7 })).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::BAD_REQUEST, Some("request_invalid")), "wrong field type");
    }

    #[tokio::test]
    async fn one_address_is_limited_on_the_public_routes() {
        let dir = tempfile::tempdir().unwrap();
        let (svc, _) = service(dir.path());
        let app = router(Arc::new(Mutex::new(svc)));
        let from = |ip: [u8; 4]| {
            let mut request = Request::builder().method("POST").uri("/v1/forgot").header("content-type", "application/json")
                .body(Body::from(json!({ "email": "nobody@example.si" }).to_string())).unwrap();
            request.extensions_mut().insert(ConnectInfo(SocketAddr::from((ip, 40000))));
            request
        };
        for _ in 0..PUBLIC_REQUESTS_PER_MINUTE {
            assert_eq!(send(&app, from([10, 0, 0, 1])).await.0, StatusCode::ACCEPTED);
        }
        let (status, body) = send(&app, from([10, 0, 0, 1])).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::TOO_MANY_REQUESTS, Some("too_many_requests")));
        assert_eq!(send(&app, from([10, 0, 0, 2])).await.0, StatusCode::ACCEPTED, "another address is not limited");
        let mut limit = PublicLimit::default();
        let address = IpAddr::from([10, 0, 0, 1]);
        assert!((0..PUBLIC_REQUESTS_PER_MINUTE).all(|_| limit.admit(address, 120)));
        assert!(!limit.admit(address, 179));
        assert!(limit.admit(address, 180), "a new minute");
        // QA 2026-11-19, AR5: one IPv6 /64 is one address; another /64 is not.
        let mut limit = PublicLimit::default();
        let host = |last: u16, net: u16| IpAddr::from([0x2001, 0xdb8, 0, net, 0, 0, 0, last]);
        assert!((0..PUBLIC_REQUESTS_PER_MINUTE).all(|i| limit.admit(host(i as u16, 1), 240)));
        assert!(!limit.admit(host(9999, 1), 241), "a new interface address in the same /64");
        assert!(limit.admit(host(1, 2), 241), "another /64");
        assert_eq!(limit_bucket(IpAddr::from([0, 0, 0, 0, 0, 0xffff, 0x0a00, 0x0001])), IpAddr::from([10, 0, 0, 1]), "mapped IPv4");
    }

    #[tokio::test]
    async fn unknown_routes_and_methods_answer_in_the_error_format() {
        let dir = tempfile::tempdir().unwrap();
        let (svc, _) = service(dir.path());
        let app = router(Arc::new(Mutex::new(svc)));
        let (status, body) = call(&app, "POST", "/v1/nothing", None, json!({})).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::NOT_FOUND, Some("not_found")));
        let (status, body) = call(&app, "GET", "/v1/login", None, json!({})).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::METHOD_NOT_ALLOWED, Some("method_not_allowed")));
    }

    /// QA 2026-11-19, AR2: at most `parallel` checks run and `parallel * HASH_QUEUE_PER_SLOT` wait; the rest are
    /// refused at once and never count against the username.
    #[tokio::test]
    async fn the_hash_gate_admits_a_short_queue_and_refuses_the_rest() {
        let gate = HashGate::new(1);
        let places: Vec<_> = (0..HASH_QUEUE_PER_SLOT).map(|_| gate.admit().expect("a place")).collect();
        assert_eq!(gate.admit().unwrap_err().code, "service_busy");
        drop(places);
        assert!(gate.admit().is_ok(), "places come back");
        let dir = tempfile::tempdir().unwrap();
        let (svc, _) = service(dir.path());
        let shared = Arc::new(Mutex::new(svc));
        let full = HashGate::new(1);
        let _held: Vec<_> = (0..HASH_QUEUE_PER_SLOT).map(|_| full.admit().unwrap()).collect();
        let app = router_with(shared.clone(), full);
        for _ in 0..crate::service::LOGIN_FAILURES + 1 {
            let (status, body) = call(&app, "POST", "/v1/login", None, json!({ "username": "ana_k", "password": "x" })).await;
            assert_eq!((status, body["error"].as_str()), (StatusCode::SERVICE_UNAVAILABLE, Some("service_busy")));
        }
        let check = shared.lock().unwrap().login_begin("ana_k", now());
        assert!(check.is_ok(), "refused at the gate, the attempts never locked the username");
    }

    /// A burst of sign-ins with made-up names (each a full Argon2 check) must not hold up the game server's redeem.
    /// 600 is more than Tokio's blocking pool (512): before the hash gate (AR2) they all ran at once.
    #[tokio::test(flavor = "multi_thread", worker_threads = 4)]
    async fn a_sign_in_burst_does_not_starve_the_redeem() {
        let dir = tempfile::tempdir().unwrap();
        let (svc, _) = service(dir.path());
        let app = router_with(Arc::new(Mutex::new(svc)), HashGate::new(2));
        // One check alone, for scale (debug builds hash slowly).
        let started = std::time::Instant::now();
        call(&app, "POST", "/v1/login", None, json!({ "username": "warmup", "password": "drava-bridge-1990" })).await;
        let one_check = started.elapsed();
        let burst: Vec<_> = (0..600)
            .map(|i| {
                let app = app.clone();
                tokio::spawn(async move {
                    call(&app, "POST", "/v1/login", None, json!({ "username": format!("guess{i}"), "password": "drava-bridge-1990" })).await
                })
            })
            .collect();
        tokio::time::sleep(std::time::Duration::from_millis(20)).await;
        let started = std::time::Instant::now();
        let (status, _) = call(&app, "POST", "/v1/server/redeem", Some(&format!("MB2-Server {KEY}")), json!({ "ticket": "forged" })).await;
        let redeem = started.elapsed();
        assert_eq!(status, StatusCode::FORBIDDEN);
        let (mut checked, mut busy) = (0, 0);
        for task in burst {
            match task.await.unwrap().0 {
                StatusCode::UNAUTHORIZED => checked += 1,
                StatusCode::SERVICE_UNAVAILABLE => busy += 1,
                other => panic!("unexpected {other}"),
            }
        }
        // Two run and eight wait; the rest are turned away rather than holding 19 MiB each.
        assert!(busy > 0 && checked >= 2 * HASH_QUEUE_PER_SLOT, "checked {checked}, busy {busy}");
        // Under one lock the redeem waited for all the checks in a row; now it waits for a few short locked steps.
        assert!(redeem < one_check * 4, "redeem took {redeem:?}; one check takes {one_check:?}");
    }

    #[tokio::test]
    async fn the_whole_flow_over_http() {
        let dir = tempfile::tempdir().unwrap();
        let (svc, inbox) = service(dir.path());
        let app = router(Arc::new(Mutex::new(svc)));
        let (status, _) = call(&app, "POST", "/v1/register", None, json!({ "email": "ana@example.si" })).await;
        assert_eq!(status, StatusCode::ACCEPTED);
        let (status, body) = call(&app, "POST", "/v1/verify", None, json!({ "email": "ana@example.si", "code": "XXXX2222" })).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::BAD_REQUEST, Some("code_invalid")));
        let (_, body) = call(&app, "POST", "/v1/verify", None, json!({ "email": "ana@example.si", "code": inbox.last_code() })).await;
        let ticket = body["ticket"].as_str().unwrap().to_string();
        let (status, _) = call(&app, "POST", "/v1/setup", None,
            json!({ "ticket": ticket, "username": "ana_k", "password": "drava-bridge-1990" })).await;
        assert_eq!(status, StatusCode::CREATED);
        let (status, body) = call(&app, "POST", "/v1/login", None, json!({ "username": "ana_k", "password": "drava-bridge-1990" })).await;
        assert_eq!(status, StatusCode::OK);
        let bearer = format!("Bearer {}", body["session"].as_str().unwrap());
        assert!(body["account"]["character"].is_null());
        let (status, _) = call(&app, "GET", "/v1/me", Some("Bearer forged"), json!({})).await;
        assert_eq!(status, StatusCode::UNAUTHORIZED);
        let (status, body) = call(&app, "POST", "/v1/character", Some(&bearer),
            json!({ "request": "r1", "first_name": "Špela", "last_name": "Kos", "model": "manny" })).await;
        assert_eq!((status, body["character"]["first_name"].as_str()), (StatusCode::OK, Some("Špela")));
        let (status, body) = call(&app, "POST", "/v1/admission", Some(&bearer), json!({})).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::BAD_REQUEST, Some("request_invalid")), "the server must be named");
        let (status, body) = call(&app, "POST", "/v1/admission", Some(&bearer), json!({ "server": "evil.example.si" })).await;
        assert_eq!((status, body["error"].as_str()), (StatusCode::FORBIDDEN, Some("server_unknown")));
        let (_, body) = call(&app, "POST", "/v1/admission", Some(&bearer), json!({ "server": SERVER })).await;
        let ticket = body["ticket"].as_str().unwrap().to_string();
        let key = body["key"].as_str().unwrap().to_string();
        let (status, _) = call(&app, "POST", "/v1/server/redeem", Some(&format!("MB2-Server {OTHER_KEY}")), json!({ "ticket": ticket })).await;
        assert_eq!(status, StatusCode::FORBIDDEN, "another server's key");
        let (status, _) = call(&app, "POST", "/v1/server/redeem", Some(&bearer), json!({ "ticket": ticket })).await;
        assert_eq!(status, StatusCode::UNAUTHORIZED, "a player session is not the server key");
        let (status, body) = call(&app, "POST", "/v1/server/redeem", Some(&format!("MB2-Server {KEY}")), json!({ "ticket": ticket })).await;
        assert_eq!((status, body["character"]["model"].as_str()), (StatusCode::OK, Some("manny")));
        assert_eq!(body["key"].as_str(), Some(key.as_str()), "the server gets the client's connection key (ADR-083)");
        let (status, _) = call(&app, "POST", "/v1/logout", Some(&bearer), json!({})).await;
        assert_eq!(status, StatusCode::OK);
        let (status, _) = call(&app, "GET", "/v1/me", Some(&bearer), json!({})).await;
        assert_eq!(status, StatusCode::UNAUTHORIZED);
    }
}
