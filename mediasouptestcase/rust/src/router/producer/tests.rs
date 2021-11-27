use crate::data_structures::TransportListenIp;
use crate::producer::ProducerOptions;
use crate::router::{Router, RouterOptions};
use crate::rtp_parameters::{
    MediaKind, MimeTypeAudio, RtpCodecCapability, RtpCodecParameters, RtpCodecParametersParameters,
    RtpParameters,
};
use crate::transport::Transport;
use crate::webrtc_transport::{TransportListenIps, WebRtcTransport, WebRtcTransportOptions};
use crate::worker::WorkerSettings;
use crate::worker_manager::WorkerManager;
use futures_lite::future;
use std::env;
use std::num::{NonZeroU32, NonZeroU8};

fn media_codecs() -> Vec<RtpCodecCapability> {
    vec![RtpCodecCapability::Audio {
        mime_type: MimeTypeAudio::Opus,
        preferred_payload_type: None,
        clock_rate: NonZeroU32::new(48000).unwrap(),
        channels: NonZeroU8::new(2).unwrap(),
        parameters: RtpCodecParametersParameters::default(),
        rtcp_feedback: vec![],
    }]
}

fn audio_producer_options() -> ProducerOptions {
    ProducerOptions::new(
        MediaKind::Audio,
        RtpParameters {
            mid: Some("AUDIO".to_string()),
            codecs: vec![RtpCodecParameters::Audio {
                mime_type: MimeTypeAudio::Opus,
                payload_type: 0,
                clock_rate: NonZeroU32::new(48000).unwrap(),
                channels: NonZeroU8::new(2).unwrap(),
                parameters: RtpCodecParametersParameters::default(),
                rtcp_feedback: vec![],
            }],
            ..RtpParameters::default()
        },
    )
}

async fn init() -> (Router, WebRtcTransport) {
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
        .create_router(RouterOptions::new(media_codecs()))
        .await
        .expect("Failed to create router");

    let transport_options =
        WebRtcTransportOptions::new(TransportListenIps::new(TransportListenIp {
            ip: "127.0.0.1".parse().unwrap(),
            announced_ip: None,
        }));

    let transport_1 = router
        .create_webrtc_transport(transport_options.clone())
        .await
        .expect("Failed to create transport1");

    (router, transport_1)
}

#[test]
fn transport_close_event() {
    future::block_on(async move {
        let (router, transport_1) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let (mut close_tx, close_rx) = async_oneshot::oneshot::<()>();
        let _handler = audio_producer.on_close(move || {
            let _ = close_tx.send(());
        });

        let (mut transport_close_tx, transport_close_rx) = async_oneshot::oneshot::<()>();
        let _handler = audio_producer.on_transport_close(move || {
            let _ = transport_close_tx.send(());
        });

        router.close();

        transport_close_rx
            .await
            .expect("Failed to receive transport_close event");
        close_rx.await.expect("Failed to receive close event");

        assert_eq!(audio_producer.closed(), true);
    });
}
