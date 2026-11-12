//! The HTTPS interface (ADR-055). JSON in and out; a refusal is `{"error": "<code>"}` with its status. A player's
//! session travels as `Authorization: Bearer`, the game server's key as `Authorization: MB2-Server`. Nothing
//! secret is logged.

use crate::service::{Outcome, Service};
use axum::extract::State;
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::Deserialize;
use serde_json::json;
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

pub type Shared = Arc<Mutex<Service>>;

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

/// Runs a rule off the async threads: password hashing takes tens of milliseconds.
async fn run<T: Send + 'static>(shared: &Shared, rule: impl FnOnce(&mut Service, i64) -> T + Send + 'static) -> T {
    let shared = shared.clone();
    tokio::task::spawn_blocking(move || {
        let mut service = shared.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        rule(&mut service, now())
    })
    .await
    .expect("account rule panicked")
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

#[derive(Deserialize)]
struct RedeemBody {
    ticket: String,
}

pub fn router(shared: Shared) -> Router {
    Router::new()
        .route("/v1/health", get(|| async { Json(json!({ "service": "mb2-accounts", "api": 1 })) }))
        .route("/v1/register", post(|State(s): State<Shared>, Json(b): Json<EmailBody>| async move {
            answer(run(&s, move |svc, now| svc.register(&b.email, now).map(|_| json!({ "sent": true }))).await, StatusCode::ACCEPTED)
        }))
        .route("/v1/verify", post(|State(s): State<Shared>, Json(b): Json<VerifyBody>| async move {
            answer(run(&s, move |svc, now| svc.verify(&b.email, &b.code, now)).await, StatusCode::OK)
        }))
        .route("/v1/setup", post(|State(s): State<Shared>, Json(b): Json<SetupBody>| async move {
            answer(run(&s, move |svc, now| svc.setup(&b.ticket, &b.username, &b.password, now)).await, StatusCode::CREATED)
        }))
        .route("/v1/login", post(|State(s): State<Shared>, Json(b): Json<LoginBody>| async move {
            answer(run(&s, move |svc, now| svc.login(&b.username, &b.password, now)).await, StatusCode::OK)
        }))
        .route("/v1/me", get(|State(s): State<Shared>, h: HeaderMap| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.me(&token, now)).await, StatusCode::OK)
        }))
        .route("/v1/logout", post(|State(s): State<Shared>, h: HeaderMap| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.logout(&token, now).map(|_| json!({}))).await, StatusCode::OK)
        }))
        .route("/v1/character", post(|State(s): State<Shared>, h: HeaderMap, Json(b): Json<CharacterBody>| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.create_character(&token, &b.request, &b.first_name, &b.last_name, &b.model, now)).await,
                StatusCode::OK)
        }))
        .route("/v1/admission", post(|State(s): State<Shared>, h: HeaderMap| async move {
            let token = credential(&h, "Bearer ").to_string();
            answer(run(&s, move |svc, now| svc.admission(&token, now)).await, StatusCode::OK)
        }))
        .route("/v1/forgot", post(|State(s): State<Shared>, Json(b): Json<EmailBody>| async move {
            answer(run(&s, move |svc, now| svc.forgot(&b.email, now).map(|_| json!({ "sent": true }))).await, StatusCode::ACCEPTED)
        }))
        .route("/v1/reset", post(|State(s): State<Shared>, Json(b): Json<ResetBody>| async move {
            answer(run(&s, move |svc, now| svc.reset(&b.email, &b.code, &b.password, now).map(|_| json!({}))).await, StatusCode::OK)
        }))
        .route("/v1/server/redeem", post(|State(s): State<Shared>, h: HeaderMap, Json(b): Json<RedeemBody>| async move {
            let key = credential(&h, "MB2-Server ").to_string();
            answer(run(&s, move |svc, now| svc.redeem(&key, &b.ticket, now)).await, StatusCode::OK)
        }))
        .with_state(shared)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::service::tests::{service, KEY};
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
        let (_, body) = call(&app, "POST", "/v1/admission", Some(&bearer), json!({})).await;
        let ticket = body["ticket"].as_str().unwrap().to_string();
        let (status, _) = call(&app, "POST", "/v1/server/redeem", Some(&bearer), json!({ "ticket": ticket })).await;
        assert_eq!(status, StatusCode::UNAUTHORIZED, "a player session is not the server key");
        let (status, body) = call(&app, "POST", "/v1/server/redeem", Some(&format!("MB2-Server {KEY}")), json!({ "ticket": ticket })).await;
        assert_eq!((status, body["character"]["model"].as_str()), (StatusCode::OK, Some("manny")));
        let (status, _) = call(&app, "POST", "/v1/logout", Some(&bearer), json!({})).await;
        assert_eq!(status, StatusCode::OK);
        let (status, _) = call(&app, "GET", "/v1/me", Some(&bearer), json!({})).await;
        assert_eq!(status, StatusCode::UNAUTHORIZED);
    }
}
