use crate::data_structures::TransportListenIp;
use crate::plain_transport::PlainTransportOptions;
use crate::router::{Router, RouterOptions};
use crate::transport::Transport;
use crate::worker::WorkerSettings;
use crate::worker_manager::WorkerManager;
use futures_lite::future;
use std::env;

async fn init() -> Router {
    {
        let mut builder = env_logger::builder();
        if env::var(env_logger::DEFAULT_FILTER_ENV).is_err() {
            builder.filter_level(log::LevelFilter::Off);
        }
        let _ = builder.is_test(true).try_init();
    }

    let worker_manager = WorkerManager::new();

    let worker = worker_manager
        .create_worker(WorkerSettings::default())
        .await
        .expect("Failed to create worker");

    let router = worker
        .create_router(RouterOptions::default())
        .await
        .expect("Failed to create router");

    router
}

#[test]
fn router_close_event() {
    future::block_on(async move {
        let router = init().await;

        let transport = router
            .create_plain_transport({
                let mut plain_transport_options = PlainTransportOptions::new(TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("4.4.4.4".parse().unwrap()),
                });
                plain_transport_options.rtcp_mux = false;

                plain_transport_options
            })
            .await
            .expect("Failed to create Plain transport");

        let (mut close_tx, close_rx) = async_oneshot::oneshot::<()>();
        let _handler = transport.on_close(Box::new(move || {
            let _ = close_tx.send(());
        }));

        let (mut router_close_tx, router_close_rx) = async_oneshot::oneshot::<()>();
        let _handler = transport.on_router_close(Box::new(move || {
            let _ = router_close_tx.send(());
        }));

        router.close();

        router_close_rx
            .await
            .expect("Failed to receive router_close event");
        close_rx.await.expect("Failed to receive close event");

        assert_eq!(transport.closed(), true);
    });
}
