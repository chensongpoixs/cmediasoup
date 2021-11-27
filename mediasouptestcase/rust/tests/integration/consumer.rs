use async_io::Timer;
use futures_lite::future;
use mediasoup::consumer::{
    ConsumableRtpEncoding, ConsumerLayers, ConsumerOptions, ConsumerScore, ConsumerType,
};
use mediasoup::data_structures::{AppData, TransportListenIp};
use mediasoup::producer::ProducerOptions;
use mediasoup::router::{Router, RouterOptions};
use mediasoup::rtp_parameters::{
    MediaKind, MimeType, MimeTypeAudio, MimeTypeVideo, RtcpFeedback, RtcpParameters,
    RtpCapabilities, RtpCodecCapability, RtpCodecParameters, RtpCodecParametersParameters,
    RtpEncodingParameters, RtpEncodingParametersRtx, RtpHeaderExtension,
    RtpHeaderExtensionDirection, RtpHeaderExtensionParameters, RtpHeaderExtensionUri,
    RtpParameters,
};
use mediasoup::scalability_modes::ScalabilityMode;
use mediasoup::transport::{ConsumeError, Transport, TransportGeneric};
use mediasoup::webrtc_transport::{TransportListenIps, WebRtcTransport, WebRtcTransportOptions};
use mediasoup::worker::{Worker, WorkerSettings};
use mediasoup::worker_manager::WorkerManager;
use parking_lot::Mutex;
use std::collections::{HashMap, HashSet};
use std::env;
use std::num::{NonZeroU32, NonZeroU8};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::Duration;

struct ProducerAppData {
    _foo: i32,
    _bar: &'static str,
}

struct ConsumerAppData {
    baz: &'static str,
}

fn media_codecs() -> Vec<RtpCodecCapability> {
    vec![
        RtpCodecCapability::Audio {
            mime_type: MimeTypeAudio::Opus,
            preferred_payload_type: None,
            clock_rate: NonZeroU32::new(48000).unwrap(),
            channels: NonZeroU8::new(2).unwrap(),
            parameters: RtpCodecParametersParameters::from([("foo", "bar".into())]),
            rtcp_feedback: vec![],
        },
        RtpCodecCapability::Video {
            mime_type: MimeTypeVideo::Vp8,
            preferred_payload_type: None,
            clock_rate: NonZeroU32::new(90000).unwrap(),
            parameters: RtpCodecParametersParameters::default(),
            rtcp_feedback: vec![],
        },
        RtpCodecCapability::Video {
            mime_type: MimeTypeVideo::H264,
            preferred_payload_type: None,
            clock_rate: NonZeroU32::new(90000).unwrap(),
            parameters: RtpCodecParametersParameters::from([
                ("level-asymmetry-allowed", 1_u32.into()),
                ("packetization-mode", 1_u32.into()),
                ("profile-level-id", "4d0032".into()),
                ("foo", "bar".into()),
            ]),
            rtcp_feedback: vec![],
        },
    ]
}

fn audio_producer_options() -> ProducerOptions {
    let mut options = ProducerOptions::new(
        MediaKind::Audio,
        RtpParameters {
            mid: Some("AUDIO".to_string()),
            codecs: vec![RtpCodecParameters::Audio {
                mime_type: MimeTypeAudio::Opus,
                payload_type: 111,
                clock_rate: NonZeroU32::new(48000).unwrap(),
                channels: NonZeroU8::new(2).unwrap(),
                parameters: RtpCodecParametersParameters::from([
                    ("useinbandfec", 1_u32.into()),
                    ("usedtx", 1_u32.into()),
                    ("foo", "222.222".into()),
                    ("bar", "333".into()),
                ]),
                rtcp_feedback: vec![],
            }],
            header_extensions: vec![
                RtpHeaderExtensionParameters {
                    uri: RtpHeaderExtensionUri::Mid,
                    id: 10,
                    encrypt: false,
                },
                RtpHeaderExtensionParameters {
                    uri: RtpHeaderExtensionUri::AudioLevel,
                    id: 12,
                    encrypt: false,
                },
            ],
            encodings: vec![RtpEncodingParameters {
                ssrc: Some(11111111),
                ..RtpEncodingParameters::default()
            }],
            rtcp: RtcpParameters {
                cname: Some("FOOBAR".to_string()),
                ..RtcpParameters::default()
            },
        },
    );

    options.app_data = AppData::new(ProducerAppData { _foo: 1, _bar: "2" });

    options
}

fn video_producer_options() -> ProducerOptions {
    let mut options = ProducerOptions::new(
        MediaKind::Video,
        RtpParameters {
            mid: Some("VIDEO".to_string()),
            codecs: vec![
                RtpCodecParameters::Video {
                    mime_type: MimeTypeVideo::H264,
                    payload_type: 112,
                    clock_rate: NonZeroU32::new(90000).unwrap(),
                    parameters: RtpCodecParametersParameters::from([
                        ("packetization-mode", 1_u32.into()),
                        ("profile-level-id", "4d0032".into()),
                    ]),
                    rtcp_feedback: vec![
                        RtcpFeedback::Nack,
                        RtcpFeedback::NackPli,
                        RtcpFeedback::GoogRemb,
                    ],
                },
                RtpCodecParameters::Video {
                    mime_type: MimeTypeVideo::Rtx,
                    payload_type: 113,
                    clock_rate: NonZeroU32::new(90000).unwrap(),
                    parameters: RtpCodecParametersParameters::from([("apt", 112u32.into())]),
                    rtcp_feedback: vec![],
                },
            ],
            header_extensions: vec![
                RtpHeaderExtensionParameters {
                    uri: RtpHeaderExtensionUri::Mid,
                    id: 10,
                    encrypt: false,
                },
                RtpHeaderExtensionParameters {
                    uri: RtpHeaderExtensionUri::VideoOrientation,
                    id: 13,
                    encrypt: false,
                },
            ],
            encodings: vec![
                RtpEncodingParameters {
                    ssrc: Some(22222222),
                    rtx: Some(RtpEncodingParametersRtx { ssrc: 22222223 }),
                    ..RtpEncodingParameters::default()
                },
                RtpEncodingParameters {
                    ssrc: Some(22222224),
                    rtx: Some(RtpEncodingParametersRtx { ssrc: 22222225 }),
                    ..RtpEncodingParameters::default()
                },
                RtpEncodingParameters {
                    ssrc: Some(22222226),
                    rtx: Some(RtpEncodingParametersRtx { ssrc: 22222227 }),
                    ..RtpEncodingParameters::default()
                },
                RtpEncodingParameters {
                    ssrc: Some(22222228),
                    rtx: Some(RtpEncodingParametersRtx { ssrc: 22222229 }),
                    ..RtpEncodingParameters::default()
                },
            ],
            rtcp: RtcpParameters {
                cname: Some("FOOBAR".to_string()),
                ..RtcpParameters::default()
            },
        },
    );

    options.app_data = AppData::new(ProducerAppData { _foo: 1, _bar: "2" });

    options
}

fn consumer_device_capabilities() -> RtpCapabilities {
    RtpCapabilities {
        codecs: vec![
            RtpCodecCapability::Audio {
                mime_type: MimeTypeAudio::Opus,
                preferred_payload_type: Some(100),
                clock_rate: NonZeroU32::new(48000).unwrap(),
                channels: NonZeroU8::new(2).unwrap(),
                parameters: RtpCodecParametersParameters::default(),
                rtcp_feedback: vec![],
            },
            RtpCodecCapability::Video {
                mime_type: MimeTypeVideo::H264,
                preferred_payload_type: Some(101),
                clock_rate: NonZeroU32::new(90000).unwrap(),
                parameters: RtpCodecParametersParameters::from([
                    ("level-asymmetry-allowed", 1_u32.into()),
                    ("packetization-mode", 1_u32.into()),
                    ("profile-level-id", "4d0032".into()),
                ]),
                rtcp_feedback: vec![
                    RtcpFeedback::Nack,
                    RtcpFeedback::NackPli,
                    RtcpFeedback::CcmFir,
                    RtcpFeedback::GoogRemb,
                ],
            },
            RtpCodecCapability::Video {
                mime_type: MimeTypeVideo::Rtx,
                preferred_payload_type: Some(102),
                clock_rate: NonZeroU32::new(90000).unwrap(),
                parameters: RtpCodecParametersParameters::from([("apt", 101_u32.into())]),
                rtcp_feedback: vec![],
            },
        ],
        header_extensions: vec![
            RtpHeaderExtension {
                kind: MediaKind::Audio,
                uri: RtpHeaderExtensionUri::Mid,
                preferred_id: 1,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Video,
                uri: RtpHeaderExtensionUri::Mid,
                preferred_id: 1,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Video,
                uri: RtpHeaderExtensionUri::RtpStreamId,
                preferred_id: 2,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Audio,
                uri: RtpHeaderExtensionUri::AbsSendTime,
                preferred_id: 4,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Video,
                uri: RtpHeaderExtensionUri::AbsSendTime,
                preferred_id: 4,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Audio,
                uri: RtpHeaderExtensionUri::AudioLevel,
                preferred_id: 10,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Video,
                uri: RtpHeaderExtensionUri::VideoOrientation,
                preferred_id: 11,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
            RtpHeaderExtension {
                kind: MediaKind::Video,
                uri: RtpHeaderExtensionUri::TimeOffset,
                preferred_id: 12,
                preferred_encrypt: false,
                direction: RtpHeaderExtensionDirection::default(),
            },
        ],
    }
}

async fn init() -> (Worker, Router, WebRtcTransport, WebRtcTransport) {
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

    let transport_2 = router
        .create_webrtc_transport(transport_options)
        .await
        .expect("Failed to create transport2");

    (worker, router, transport_1, transport_2)
}

#[test]
fn consume_succeeds() {
    future::block_on(async move {
        let (_worker, router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let video_producer = transport_1
            .produce(video_producer_options())
            .await
            .expect("Failed to produce video");

        video_producer
            .pause()
            .await
            .expect("Failed to pause video producer");

        let new_consumer_count = Arc::new(AtomicUsize::new(0));

        transport_2
            .on_new_consumer({
                let new_consumer_count = Arc::clone(&new_consumer_count);

                Box::new(move |_consumer| {
                    new_consumer_count.fetch_add(1, Ordering::SeqCst);
                })
            })
            .detach();

        let consumer_device_capabilities = consumer_device_capabilities();

        let audio_consumer;
        {
            assert!(router.can_consume(&audio_producer.id(), &consumer_device_capabilities));

            audio_consumer = transport_2
                .consume({
                    let mut options = ConsumerOptions::new(
                        audio_producer.id(),
                        consumer_device_capabilities.clone(),
                    );
                    options.app_data = AppData::new(ConsumerAppData { baz: "LOL" });
                    options
                })
                .await
                .expect("Failed to consume audio");

            assert_eq!(new_consumer_count.load(Ordering::SeqCst), 1);
            assert_eq!(audio_consumer.producer_id(), audio_producer.id());
            assert_eq!(audio_consumer.closed(), false);
            assert_eq!(audio_consumer.kind(), MediaKind::Audio);
            assert_eq!(audio_consumer.rtp_parameters().mid, Some("0".to_string()));
            assert_eq!(
                audio_consumer.rtp_parameters().codecs,
                vec![RtpCodecParameters::Audio {
                    mime_type: MimeTypeAudio::Opus,
                    payload_type: 100,
                    clock_rate: NonZeroU32::new(48000).unwrap(),
                    channels: NonZeroU8::new(2).unwrap(),
                    parameters: RtpCodecParametersParameters::from([
                        ("useinbandfec", 1_u32.into()),
                        ("usedtx", 1_u32.into()),
                        ("foo", "222.222".into()),
                        ("bar", "333".into()),
                    ]),
                    rtcp_feedback: vec![],
                }]
            );
            assert_eq!(audio_consumer.r#type(), ConsumerType::Simple);
            assert_eq!(audio_consumer.paused(), false);
            assert_eq!(audio_consumer.producer_paused(), false);
            assert_eq!(audio_consumer.priority(), 1);
            assert_eq!(
                audio_consumer.score(),
                ConsumerScore {
                    score: 10,
                    producer_score: 0,
                    producer_scores: vec![0]
                }
            );
            assert_eq!(audio_consumer.preferred_layers(), None);
            assert_eq!(audio_consumer.current_layers(), None);
            assert_eq!(
                audio_consumer
                    .app_data()
                    .downcast_ref::<ConsumerAppData>()
                    .unwrap()
                    .baz,
                "LOL"
            );

            let router_dump = router.dump().await.expect("Failed to get router dump");

            assert_eq!(router_dump.map_producer_id_consumer_ids, {
                let mut map = HashMap::new();
                map.insert(audio_producer.id(), {
                    let mut set = HashSet::new();
                    set.insert(audio_consumer.id());
                    set
                });
                map.insert(video_producer.id(), HashSet::new());
                map
            });

            let transport_2_dump = transport_2
                .dump()
                .await
                .expect("Failed to get transport 2 dump");

            assert_eq!(transport_2_dump.producer_ids, vec![]);
            assert_eq!(transport_2_dump.consumer_ids, vec![audio_consumer.id()]);
        }

        let video_consumer;
        {
            assert!(router.can_consume(&video_producer.id(), &consumer_device_capabilities));

            video_consumer = transport_2
                .consume({
                    let mut options = ConsumerOptions::new(
                        video_producer.id(),
                        consumer_device_capabilities.clone(),
                    );
                    options.paused = true;
                    options.preferred_layers = Some(ConsumerLayers {
                        spatial_layer: 12,
                        temporal_layer: None,
                    });
                    options.app_data = AppData::new(ConsumerAppData { baz: "LOL" });
                    options
                })
                .await
                .expect("Failed to consume video");

            assert_eq!(new_consumer_count.load(Ordering::SeqCst), 2);
            assert_eq!(video_consumer.producer_id(), video_producer.id());
            assert_eq!(video_consumer.closed(), false);
            assert_eq!(video_consumer.kind(), MediaKind::Video);
            assert_eq!(video_consumer.rtp_parameters().mid, Some("1".to_string()));
            assert_eq!(
                video_consumer.rtp_parameters().codecs,
                vec![
                    RtpCodecParameters::Video {
                        mime_type: MimeTypeVideo::H264,
                        payload_type: 103,
                        clock_rate: NonZeroU32::new(90000).unwrap(),
                        parameters: RtpCodecParametersParameters::from([
                            ("packetization-mode", 1_u32.into()),
                            ("profile-level-id", "4d0032".into()),
                        ]),
                        rtcp_feedback: vec![
                            RtcpFeedback::Nack,
                            RtcpFeedback::NackPli,
                            RtcpFeedback::CcmFir,
                            RtcpFeedback::GoogRemb,
                        ],
                    },
                    RtpCodecParameters::Video {
                        mime_type: MimeTypeVideo::Rtx,
                        payload_type: 104,
                        clock_rate: NonZeroU32::new(90000).unwrap(),
                        parameters: RtpCodecParametersParameters::from([("apt", 103u32.into())]),
                        rtcp_feedback: vec![],
                    },
                ]
            );
            assert_eq!(video_consumer.r#type(), ConsumerType::Simulcast);
            assert_eq!(video_consumer.paused(), true);
            assert_eq!(video_consumer.producer_paused(), true);
            assert_eq!(video_consumer.priority(), 1);
            assert_eq!(
                video_consumer.score(),
                ConsumerScore {
                    score: 10,
                    producer_score: 0,
                    producer_scores: vec![0, 0, 0, 0]
                }
            );
            assert_eq!(
                video_consumer.preferred_layers(),
                Some(ConsumerLayers {
                    spatial_layer: 3,
                    temporal_layer: Some(0)
                })
            );
            assert_eq!(video_consumer.current_layers(), None);
            assert_eq!(
                video_consumer
                    .app_data()
                    .downcast_ref::<ConsumerAppData>()
                    .unwrap()
                    .baz,
                "LOL"
            );

            let router_dump = router.dump().await.expect("Failed to get router dump");

            assert_eq!(router_dump.map_producer_id_consumer_ids, {
                let mut map = HashMap::new();
                map.insert(audio_producer.id(), {
                    let mut set = HashSet::new();
                    set.insert(audio_consumer.id());
                    set
                });
                map.insert(video_producer.id(), {
                    let mut set = HashSet::new();
                    set.insert(video_consumer.id());
                    set
                });
                map
            });

            let transport_2_dump = transport_2
                .dump()
                .await
                .expect("Failed to get transport 2 dump");

            assert_eq!(transport_2_dump.producer_ids, vec![]);
            assert_eq!(
                transport_2_dump.consumer_ids.clone().sort(),
                vec![audio_consumer.id(), video_consumer.id()].sort()
            );
        }

        {
            assert!(router.can_consume(&video_producer.id(), &consumer_device_capabilities));

            let video_pipe_consumer = transport_2
                .consume({
                    let mut options = ConsumerOptions::new(
                        video_producer.id(),
                        consumer_device_capabilities.clone(),
                    );
                    options.pipe = true;
                    options
                })
                .await
                .expect("Failed to consume video");

            assert_eq!(new_consumer_count.load(Ordering::SeqCst), 3);
            assert_eq!(video_pipe_consumer.producer_id(), video_producer.id());
            assert_eq!(video_pipe_consumer.closed(), false);
            assert_eq!(video_pipe_consumer.kind(), MediaKind::Video);
            assert_eq!(video_pipe_consumer.rtp_parameters().mid, None);
            assert_eq!(
                video_pipe_consumer.rtp_parameters().codecs,
                vec![
                    RtpCodecParameters::Video {
                        mime_type: MimeTypeVideo::H264,
                        payload_type: 103,
                        clock_rate: NonZeroU32::new(90000).unwrap(),
                        parameters: RtpCodecParametersParameters::from([
                            ("packetization-mode", 1_u32.into()),
                            ("profile-level-id", "4d0032".into()),
                        ]),
                        rtcp_feedback: vec![
                            RtcpFeedback::Nack,
                            RtcpFeedback::NackPli,
                            RtcpFeedback::CcmFir,
                            RtcpFeedback::GoogRemb,
                        ],
                    },
                    RtpCodecParameters::Video {
                        mime_type: MimeTypeVideo::Rtx,
                        payload_type: 104,
                        clock_rate: NonZeroU32::new(90000).unwrap(),
                        parameters: RtpCodecParametersParameters::from([("apt", 103u32.into())]),
                        rtcp_feedback: vec![],
                    },
                ]
            );
            assert_eq!(video_pipe_consumer.r#type(), ConsumerType::Pipe);
            assert_eq!(video_pipe_consumer.paused(), false);
            assert_eq!(video_pipe_consumer.producer_paused(), true);
            assert_eq!(video_pipe_consumer.priority(), 1);
            assert_eq!(
                video_pipe_consumer.score(),
                ConsumerScore {
                    score: 10,
                    producer_score: 10,
                    producer_scores: vec![0, 0, 0, 0]
                },
            );
            assert_eq!(video_pipe_consumer.preferred_layers(), None);
            assert_eq!(video_pipe_consumer.current_layers(), None);
            assert_eq!(
                video_pipe_consumer.app_data().downcast_ref::<()>().unwrap(),
                &(),
            );

            let router_dump = router.dump().await.expect("Failed to get router dump");

            assert_eq!(router_dump.map_producer_id_consumer_ids, {
                let mut map = HashMap::new();
                map.insert(audio_producer.id(), {
                    let mut set = HashSet::new();
                    set.insert(audio_consumer.id());
                    set
                });
                map.insert(video_producer.id(), {
                    let mut set = HashSet::new();
                    set.insert(video_consumer.id());
                    set.insert(video_pipe_consumer.id());
                    set
                });
                map
            });

            let transport_2_dump = transport_2
                .dump()
                .await
                .expect("Failed to get transport 2 dump");

            assert_eq!(transport_2_dump.producer_ids, vec![]);
            assert_eq!(
                transport_2_dump.consumer_ids.clone().sort(),
                vec![
                    audio_consumer.id(),
                    video_consumer.id(),
                    video_pipe_consumer.id()
                ]
                .sort(),
            );
        }
    });
}

#[test]
fn consumer_with_user_defined_mid() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let producer_1 = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let consumer_2_1 = transport_2
            .consume(ConsumerOptions::new(
                producer_1.id(),
                consumer_device_capabilities(),
            ))
            .await
            .expect("Failed to consume audio");
        assert_eq!(
            consumer_2_1.rtp_parameters().mid,
            Some("0".to_string()),
            "MID automatically assigned to sequential number"
        );

        let consumer_2_2 = transport_2
            .consume({
                let mut options =
                    ConsumerOptions::new(producer_1.id(), consumer_device_capabilities());
                options.mid = Some("custom-mid".to_owned());
                options
            })
            .await
            .expect("Failed to consume audio");
        assert_eq!(
            consumer_2_2.rtp_parameters().mid,
            Some("custom-mid".to_string()),
            "MID is assigned to user-provided value"
        );

        let consumer_2_3 = transport_2
            .consume(ConsumerOptions::new(
                producer_1.id(),
                consumer_device_capabilities(),
            ))
            .await
            .expect("Failed to consume audio");
        assert_eq!(
            consumer_2_3.rtp_parameters().mid,
            Some("1".to_string()),
            "MID automatically assigned to next sequential number"
        );
    })
}

#[test]
fn weak() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let consumer = transport_2
            .consume({
                let mut options =
                    ConsumerOptions::new(producer.id(), consumer_device_capabilities());
                options.app_data = AppData::new(ConsumerAppData { baz: "LOL" });
                options
            })
            .await
            .expect("Failed to consume audio");

        let weak_consumer = consumer.downgrade();

        assert!(weak_consumer.upgrade().is_some());

        drop(consumer);

        assert!(weak_consumer.upgrade().is_none());
    });
}

#[test]
fn consume_incompatible_rtp_capabilities() {
    future::block_on(async move {
        let (_worker, router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        {
            let incompatible_device_capabilities = RtpCapabilities {
                codecs: vec![RtpCodecCapability::Audio {
                    mime_type: MimeTypeAudio::Isac,
                    preferred_payload_type: Some(100),
                    clock_rate: NonZeroU32::new(32_000).unwrap(),
                    channels: NonZeroU8::new(1).unwrap(),
                    parameters: RtpCodecParametersParameters::default(),
                    rtcp_feedback: vec![],
                }],
                header_extensions: vec![],
            };

            assert_eq!(
                router.can_consume(&audio_producer.id(), &incompatible_device_capabilities),
                false
            );

            assert!(matches!(
                transport_2
                    .consume(ConsumerOptions::new(
                        audio_producer.id(),
                        incompatible_device_capabilities,
                    ))
                    .await,
                Err(ConsumeError::BadConsumerRtpParameters(_))
            ));
        }

        {
            let invalid_device_capabilities = RtpCapabilities {
                codecs: vec![],
                header_extensions: vec![],
            };

            assert_eq!(
                router.can_consume(&audio_producer.id(), &invalid_device_capabilities),
                false
            );

            assert!(matches!(
                transport_2
                    .consume(ConsumerOptions::new(
                        audio_producer.id(),
                        invalid_device_capabilities,
                    ))
                    .await,
                Err(ConsumeError::BadConsumerRtpParameters(_))
            ));
        }
    });
}

#[test]
fn dump_succeeds() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let video_producer = transport_1
            .produce(video_producer_options())
            .await
            .expect("Failed to produce video");

        video_producer
            .pause()
            .await
            .expect("Failed to pause video producer");

        let consumer_device_capabilities = consumer_device_capabilities();

        {
            let audio_consumer = transport_2
                .consume(ConsumerOptions::new(
                    audio_producer.id(),
                    consumer_device_capabilities.clone(),
                ))
                .await
                .expect("Failed to consume audio");

            let dump = audio_consumer
                .dump()
                .await
                .expect("Audio consumer dump failed");

            assert_eq!(dump.id, audio_consumer.id());
            assert_eq!(dump.producer_id, audio_consumer.producer_id());
            assert_eq!(dump.kind, audio_consumer.kind());
            assert_eq!(
                dump.rtp_parameters.codecs,
                vec![RtpCodecParameters::Audio {
                    mime_type: MimeTypeAudio::Opus,
                    payload_type: 100,
                    clock_rate: NonZeroU32::new(48000).unwrap(),
                    channels: NonZeroU8::new(2).unwrap(),
                    parameters: RtpCodecParametersParameters::from([
                        ("useinbandfec", 1_u32.into()),
                        ("usedtx", 1_u32.into()),
                        ("foo", "222.222".into()),
                        ("bar", "333".into()),
                    ]),
                    rtcp_feedback: vec![],
                }],
            );
            assert_eq!(
                dump.rtp_parameters.header_extensions,
                vec![
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::Mid,
                        id: 1,
                        encrypt: false,
                    },
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::AbsSendTime,
                        id: 4,
                        encrypt: false,
                    },
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::AudioLevel,
                        id: 10,
                        encrypt: false,
                    },
                ],
            );
            assert_eq!(
                dump.rtp_parameters.encodings,
                vec![RtpEncodingParameters {
                    codec_payload_type: Some(100),
                    rtx: None,
                    dtx: None,
                    scalability_mode: ScalabilityMode::None,
                    scale_resolution_down_by: None,
                    ssrc: audio_consumer
                        .rtp_parameters()
                        .encodings
                        .get(0)
                        .unwrap()
                        .ssrc,
                    rid: None,
                    max_bitrate: None,
                }],
            );
            assert_eq!(dump.r#type, ConsumerType::Simple);
            assert_eq!(
                dump.consumable_rtp_encodings,
                audio_producer
                    .consumable_rtp_parameters()
                    .encodings
                    .iter()
                    .map(|encoding| ConsumableRtpEncoding {
                        ssrc: encoding.ssrc,
                        rid: None,
                        codec_payload_type: None,
                        rtx: None,
                        max_bitrate: None,
                        max_framerate: None,
                        dtx: None,
                        scalability_mode: ScalabilityMode::None,
                        spatial_layers: None,
                        temporal_layers: None,
                        ksvc: None
                    })
                    .collect::<Vec<_>>()
            );
        }

        {
            let video_consumer = transport_2
                .consume({
                    let mut options =
                        ConsumerOptions::new(video_producer.id(), consumer_device_capabilities);
                    options.paused = true;
                    options.preferred_layers = Some(ConsumerLayers {
                        spatial_layer: 12,
                        temporal_layer: None,
                    });
                    options
                })
                .await
                .expect("Failed to consume video");

            let dump = video_consumer
                .dump()
                .await
                .expect("Video consumer dump failed");

            assert_eq!(dump.id, video_consumer.id());
            assert_eq!(dump.producer_id, video_consumer.producer_id());
            assert_eq!(dump.kind, video_consumer.kind());
            assert_eq!(
                dump.rtp_parameters.codecs,
                vec![
                    RtpCodecParameters::Video {
                        mime_type: MimeTypeVideo::H264,
                        payload_type: 103,
                        clock_rate: NonZeroU32::new(90000).unwrap(),
                        parameters: RtpCodecParametersParameters::from([
                            ("packetization-mode", 1_u32.into()),
                            ("profile-level-id", "4d0032".into()),
                        ]),
                        rtcp_feedback: vec![
                            RtcpFeedback::Nack,
                            RtcpFeedback::NackPli,
                            RtcpFeedback::CcmFir,
                            RtcpFeedback::GoogRemb,
                        ],
                    },
                    RtpCodecParameters::Video {
                        mime_type: MimeTypeVideo::Rtx,
                        payload_type: 104,
                        clock_rate: NonZeroU32::new(90000).unwrap(),
                        parameters: RtpCodecParametersParameters::from([("apt", 103u32.into())]),
                        rtcp_feedback: vec![],
                    }
                ],
            );
            assert_eq!(
                dump.rtp_parameters.header_extensions,
                vec![
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::Mid,
                        id: 1,
                        encrypt: false,
                    },
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::AbsSendTime,
                        id: 4,
                        encrypt: false,
                    },
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::VideoOrientation,
                        id: 11,
                        encrypt: false,
                    },
                    RtpHeaderExtensionParameters {
                        uri: RtpHeaderExtensionUri::TimeOffset,
                        id: 12,
                        encrypt: false,
                    },
                ],
            );
            assert_eq!(
                dump.rtp_parameters.encodings,
                vec![RtpEncodingParameters {
                    codec_payload_type: Some(103),
                    ssrc: video_consumer
                        .rtp_parameters()
                        .encodings
                        .get(0)
                        .unwrap()
                        .ssrc,
                    rtx: video_consumer
                        .rtp_parameters()
                        .encodings
                        .get(0)
                        .unwrap()
                        .rtx,
                    dtx: None,
                    scalability_mode: "S4T1".parse().unwrap(),
                    scale_resolution_down_by: None,
                    rid: None,
                    max_bitrate: None,
                }],
            );
            assert_eq!(dump.r#type, ConsumerType::Simulcast);
            assert_eq!(
                dump.consumable_rtp_encodings,
                video_producer
                    .consumable_rtp_parameters()
                    .encodings
                    .iter()
                    .map(|encoding| ConsumableRtpEncoding {
                        ssrc: encoding.ssrc,
                        rid: None,
                        codec_payload_type: None,
                        rtx: None,
                        max_bitrate: None,
                        max_framerate: None,
                        dtx: None,
                        scalability_mode: ScalabilityMode::None,
                        spatial_layers: None,
                        temporal_layers: None,
                        ksvc: None,
                    })
                    .collect::<Vec<_>>()
            );
            assert_eq!(dump.supported_codec_payload_types, vec![103]);
            assert_eq!(dump.paused, true);
            assert_eq!(dump.producer_paused, true);
            assert_eq!(dump.priority, 1);
        }
    });
}

#[test]
fn get_stats_succeeds() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let consumer_device_capabilities = consumer_device_capabilities();

        {
            let audio_producer = transport_1
                .produce(audio_producer_options())
                .await
                .expect("Failed to produce audio");

            let audio_consumer = transport_2
                .consume(ConsumerOptions::new(
                    audio_producer.id(),
                    consumer_device_capabilities.clone(),
                ))
                .await
                .expect("Failed to consume audio");

            let stats = audio_consumer
                .get_stats()
                .await
                .expect("Audio consumer get_stats failed");

            let consumer_stat = stats.consumer_stats();

            assert_eq!(consumer_stat.kind, MediaKind::Audio);
            assert_eq!(
                consumer_stat.mime_type,
                MimeType::Audio(MimeTypeAudio::Opus)
            );
            assert_eq!(
                consumer_stat.ssrc,
                audio_consumer
                    .rtp_parameters()
                    .encodings
                    .get(0)
                    .unwrap()
                    .ssrc
                    .unwrap()
            );
        }

        {
            let video_producer = transport_1
                .produce(video_producer_options())
                .await
                .expect("Failed to produce video");

            video_producer
                .pause()
                .await
                .expect("Failed to pause video producer");

            let video_consumer = transport_2
                .consume({
                    let mut options =
                        ConsumerOptions::new(video_producer.id(), consumer_device_capabilities);
                    options.paused = true;
                    options.preferred_layers = Some(ConsumerLayers {
                        spatial_layer: 12,
                        temporal_layer: None,
                    });
                    options
                })
                .await
                .expect("Failed to consume video");

            let stats = video_consumer
                .get_stats()
                .await
                .expect("Video consumer get_stats failed");

            let consumer_stat = stats.consumer_stats();

            assert_eq!(consumer_stat.kind, MediaKind::Video);
            assert_eq!(
                consumer_stat.mime_type,
                MimeType::Video(MimeTypeVideo::H264)
            );
            assert_eq!(
                consumer_stat.ssrc,
                video_consumer
                    .rtp_parameters()
                    .encodings
                    .get(0)
                    .unwrap()
                    .ssrc
                    .unwrap()
            );
        }
    });
}

#[test]
fn pause_resume_succeeds() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let audio_consumer = transport_2
            .consume(ConsumerOptions::new(
                audio_producer.id(),
                consumer_device_capabilities(),
            ))
            .await
            .expect("Failed to consume audio");

        {
            audio_consumer
                .pause()
                .await
                .expect("Failed to pause consumer");

            let dump = audio_consumer.dump().await.expect("Consumer dump failed");

            assert_eq!(dump.paused, true);
        }

        {
            audio_consumer
                .resume()
                .await
                .expect("Failed to resume consumer");

            let dump = audio_consumer.dump().await.expect("Consumer dump failed");

            assert_eq!(dump.paused, false);
        }
    });
}

#[test]
fn set_preferred_layers_succeeds() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let consumer_device_capabilities = consumer_device_capabilities();

        {
            let audio_producer = transport_1
                .produce(audio_producer_options())
                .await
                .expect("Failed to produce audio");

            let audio_consumer = transport_2
                .consume(ConsumerOptions::new(
                    audio_producer.id(),
                    consumer_device_capabilities.clone(),
                ))
                .await
                .expect("Failed to consume audio");

            audio_consumer
                .set_preferred_layers(ConsumerLayers {
                    spatial_layer: 1,
                    temporal_layer: Some(1),
                })
                .await
                .expect("Failed to set preferred layers consumer");

            assert_eq!(audio_consumer.preferred_layers(), None);
        }

        {
            let video_producer = transport_1
                .produce(video_producer_options())
                .await
                .expect("Failed to produce audio");

            let video_consumer = transport_2
                .consume({
                    let mut options =
                        ConsumerOptions::new(video_producer.id(), consumer_device_capabilities);
                    options.paused = true;
                    options.preferred_layers = Some(ConsumerLayers {
                        spatial_layer: 12,
                        temporal_layer: None,
                    });
                    options
                })
                .await
                .expect("Failed to consume video");

            video_consumer
                .set_preferred_layers(ConsumerLayers {
                    spatial_layer: 2,
                    temporal_layer: Some(3),
                })
                .await
                .expect("Failed to set preferred layers consumer");

            assert_eq!(
                video_consumer.preferred_layers(),
                Some(ConsumerLayers {
                    spatial_layer: 2,
                    temporal_layer: Some(0),
                })
            );
        }
    });
}

#[test]
fn set_unset_priority_succeeds() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let video_producer = transport_1
            .produce(video_producer_options())
            .await
            .expect("Failed to produce audio");

        let video_consumer = transport_2
            .consume({
                let mut options =
                    ConsumerOptions::new(video_producer.id(), consumer_device_capabilities());
                options.paused = true;
                options.preferred_layers = Some(ConsumerLayers {
                    spatial_layer: 12,
                    temporal_layer: None,
                });
                options
            })
            .await
            .expect("Failed to consume video");

        video_consumer
            .set_priority(2)
            .await
            .expect("Failed to ser priority");

        assert_eq!(video_consumer.priority(), 2);

        video_consumer
            .unset_priority()
            .await
            .expect("Failed to ser priority");

        assert_eq!(video_consumer.priority(), 1);
    });
}

#[test]
fn producer_pause_resume_events() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let audio_consumer = transport_2
            .consume(ConsumerOptions::new(
                audio_producer.id(),
                consumer_device_capabilities(),
            ))
            .await
            .expect("Failed to consume audio");

        {
            let (tx, rx) = async_oneshot::oneshot::<()>();
            let _handler = audio_consumer.on_producer_pause({
                let tx = Mutex::new(Some(tx));

                move || {
                    let _ = tx.lock().take().unwrap().send(());
                }
            });
            audio_producer
                .pause()
                .await
                .expect("Failed to pause producer");
            rx.await.expect("Failed to receive producer paused event");

            assert_eq!(audio_consumer.paused(), false);
            assert_eq!(audio_consumer.producer_paused(), true);
        }

        {
            let (tx, rx) = async_oneshot::oneshot::<()>();
            let _handler = audio_consumer.on_producer_resume({
                let tx = Mutex::new(Some(tx));

                move || {
                    let _ = tx.lock().take().unwrap().send(());
                }
            });
            audio_producer
                .resume()
                .await
                .expect("Failed to pause producer");
            rx.await.expect("Failed to receive producer paused event");

            assert_eq!(audio_consumer.paused(), false);
            assert_eq!(audio_consumer.producer_paused(), false);
        }
    });
}

#[test]
fn close_event() {
    future::block_on(async move {
        let (_worker, router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let audio_consumer = transport_2
            .consume(ConsumerOptions::new(
                audio_producer.id(),
                consumer_device_capabilities(),
            ))
            .await
            .expect("Failed to consume audio");

        {
            let (mut tx, rx) = async_oneshot::oneshot::<()>();
            let _handler = audio_consumer.on_close(move || {
                let _ = tx.send(());
            });
            drop(audio_consumer);

            rx.await.expect("Failed to receive close event");
        }

        // Drop is async, give consumer a bit of time to finish
        Timer::after(Duration::from_millis(200)).await;

        {
            let dump = router.dump().await.expect("Failed to dump router");

            assert_eq!(dump.map_producer_id_consumer_ids, {
                let mut map = HashMap::new();
                map.insert(audio_producer.id(), HashSet::new());
                map
            });
            assert_eq!(dump.map_consumer_id_producer_id, HashMap::new());
        }

        {
            let dump = transport_2.dump().await.expect("Failed to dump transport");

            assert_eq!(dump.producer_ids, vec![]);
            assert_eq!(dump.consumer_ids, vec![]);
        }
    });
}

#[test]
fn producer_close_event() {
    future::block_on(async move {
        let (_worker, _router, transport_1, transport_2) = init().await;

        let audio_producer = transport_1
            .produce(audio_producer_options())
            .await
            .expect("Failed to produce audio");

        let audio_consumer = transport_2
            .consume(ConsumerOptions::new(
                audio_producer.id(),
                consumer_device_capabilities(),
            ))
            .await
            .expect("Failed to consume audio");

        let (mut close_tx, close_rx) = async_oneshot::oneshot::<()>();
        let _handler = audio_consumer.on_close(move || {
            let _ = close_tx.send(());
        });

        let (mut producer_close_tx, producer_close_rx) = async_oneshot::oneshot::<()>();
        let _handler = audio_consumer.on_producer_close(move || {
            let _ = producer_close_tx.send(());
        });
        drop(audio_producer);

        producer_close_rx
            .await
            .expect("Failed to receive producer_close event");

        close_rx.await.expect("Failed to receive close event");

        assert_eq!(audio_consumer.closed(), true);
    });
}
