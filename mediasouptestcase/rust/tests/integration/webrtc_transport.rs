use futures_lite::future;
use mediasoup::data_structures::{
    AppData, DtlsFingerprint, DtlsParameters, DtlsRole, DtlsState, IceCandidateTcpType,
    IceCandidateType, IceRole, IceState, SctpState, TransportListenIp, TransportProtocol,
};
use mediasoup::router::{Router, RouterOptions};
use mediasoup::rtp_parameters::{
    MimeTypeAudio, MimeTypeVideo, RtpCodecCapability, RtpCodecParametersParameters,
};
use mediasoup::sctp_parameters::{NumSctpStreams, SctpParameters};
use mediasoup::transport::{Transport, TransportGeneric, TransportTraceEventType};
use mediasoup::webrtc_transport::{
    TransportListenIps, WebRtcTransportOptions, WebRtcTransportRemoteParameters,
};
use mediasoup::worker::{RequestError, Worker, WorkerSettings};
use mediasoup::worker_manager::WorkerManager;
use std::collections::HashSet;
use std::convert::TryInto;
use std::env;
use std::net::IpAddr;
use std::num::{NonZeroU32, NonZeroU8};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

struct CustomAppData {
    foo: &'static str,
}

fn media_codecs() -> Vec<RtpCodecCapability> {
    vec![
        RtpCodecCapability::Audio {
            mime_type: MimeTypeAudio::Opus,
            preferred_payload_type: None,
            clock_rate: NonZeroU32::new(48000).unwrap(),
            channels: NonZeroU8::new(2).unwrap(),
            parameters: RtpCodecParametersParameters::from([
                ("useinbandfec", 1_u32.into()),
                ("foo", "bar".into()),
            ]),
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

async fn init() -> (Worker, Router) {
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

    (worker, router)
}

#[test]
fn create_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        {
            let transport = router
                .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                    TransportListenIp {
                        ip: "127.0.0.1".parse().unwrap(),
                        announced_ip: Some("9.9.9.1".parse().unwrap()),
                    },
                )))
                .await
                .expect("Failed to create WebRTC transport");

            let router_dump = router.dump().await.expect("Failed to dump router");
            assert_eq!(router_dump.transport_ids, {
                let mut set = HashSet::new();
                set.insert(transport.id());
                set
            });
        }

        {
            let new_transports_count = Arc::new(AtomicUsize::new(0));

            router
                .on_new_transport({
                    let new_transports_count = Arc::clone(&new_transports_count);

                    move |_transport| {
                        new_transports_count.fetch_add(1, Ordering::SeqCst);
                    }
                })
                .detach();

            let transport1 = router
                .create_webrtc_transport({
                    let mut webrtc_transport_options = WebRtcTransportOptions::new(
                        vec![
                            TransportListenIp {
                                ip: "127.0.0.1".parse().unwrap(),
                                announced_ip: Some("9.9.9.1".parse().unwrap()),
                            },
                            TransportListenIp {
                                ip: "0.0.0.0".parse().unwrap(),
                                announced_ip: Some("9.9.9.2".parse().unwrap()),
                            },
                            TransportListenIp {
                                ip: "127.0.0.1".parse().unwrap(),
                                announced_ip: None,
                            },
                        ]
                        .try_into()
                        .unwrap(),
                    );
                    webrtc_transport_options.enable_tcp = true;
                    webrtc_transport_options.prefer_udp = true;
                    webrtc_transport_options.enable_sctp = true;
                    webrtc_transport_options.num_sctp_streams = NumSctpStreams {
                        os: 2048,
                        mis: 2048,
                    };
                    webrtc_transport_options.max_sctp_message_size = 1000000;
                    webrtc_transport_options.app_data = AppData::new(CustomAppData { foo: "bar" });

                    webrtc_transport_options
                })
                .await
                .expect("Failed to create WebRTC transport");

            assert_eq!(new_transports_count.load(Ordering::SeqCst), 1);
            assert_eq!(
                transport1
                    .app_data()
                    .downcast_ref::<CustomAppData>()
                    .unwrap()
                    .foo,
                "bar",
            );
            assert_eq!(transport1.ice_role(), IceRole::Controlled);
            assert_eq!(transport1.ice_parameters().ice_lite, Some(true));
            assert_eq!(
                transport1.sctp_parameters(),
                Some(SctpParameters {
                    port: 5000,
                    os: 2048,
                    mis: 2048,
                    max_message_size: 1000000,
                }),
            );
            {
                let ice_candidates = transport1.ice_candidates();
                assert_eq!(ice_candidates.len(), 6);
                assert_eq!(ice_candidates[0].ip, "9.9.9.1".parse::<IpAddr>().unwrap());
                assert_eq!(ice_candidates[0].protocol, TransportProtocol::Udp);
                assert_eq!(ice_candidates[0].r#type, IceCandidateType::Host);
                assert_eq!(ice_candidates[0].tcp_type, None);
                assert_eq!(ice_candidates[1].ip, "9.9.9.1".parse::<IpAddr>().unwrap());
                assert_eq!(ice_candidates[1].protocol, TransportProtocol::Tcp);
                assert_eq!(ice_candidates[1].r#type, IceCandidateType::Host);
                assert_eq!(
                    ice_candidates[1].tcp_type,
                    Some(IceCandidateTcpType::Passive),
                );
                assert_eq!(ice_candidates[2].ip, "9.9.9.2".parse::<IpAddr>().unwrap());
                assert_eq!(ice_candidates[2].protocol, TransportProtocol::Udp);
                assert_eq!(ice_candidates[2].r#type, IceCandidateType::Host);
                assert_eq!(ice_candidates[2].tcp_type, None);
                assert_eq!(ice_candidates[3].ip, "9.9.9.2".parse::<IpAddr>().unwrap());
                assert_eq!(ice_candidates[3].protocol, TransportProtocol::Tcp);
                assert_eq!(ice_candidates[3].r#type, IceCandidateType::Host);
                assert_eq!(
                    ice_candidates[3].tcp_type,
                    Some(IceCandidateTcpType::Passive),
                );
                assert_eq!(ice_candidates[4].ip, "127.0.0.1".parse::<IpAddr>().unwrap());
                assert_eq!(ice_candidates[4].protocol, TransportProtocol::Udp);
                assert_eq!(ice_candidates[4].r#type, IceCandidateType::Host);
                assert_eq!(ice_candidates[4].tcp_type, None);
                assert_eq!(ice_candidates[4].ip, "127.0.0.1".parse::<IpAddr>().unwrap());
                assert_eq!(ice_candidates[4].protocol, TransportProtocol::Udp);
                assert_eq!(ice_candidates[4].r#type, IceCandidateType::Host);
                assert_eq!(ice_candidates[4].tcp_type, None);
                assert!(ice_candidates[0].priority > ice_candidates[1].priority);
                assert!(ice_candidates[2].priority > ice_candidates[1].priority);
                assert!(ice_candidates[2].priority > ice_candidates[3].priority);
                assert!(ice_candidates[4].priority > ice_candidates[3].priority);
                assert!(ice_candidates[4].priority > ice_candidates[5].priority);
            }

            assert_eq!(transport1.ice_state(), IceState::New);
            assert_eq!(transport1.ice_selected_tuple(), None);
            assert_eq!(transport1.dtls_parameters().role, DtlsRole::Auto);
            assert_eq!(transport1.dtls_state(), DtlsState::New);
            assert_eq!(transport1.sctp_state(), Some(SctpState::New));

            {
                let transport_dump = transport1
                    .dump()
                    .await
                    .expect("Failed to dump WebRTC transport");

                assert_eq!(transport_dump.id, transport1.id());
                assert_eq!(transport_dump.direct, false);
                assert_eq!(transport_dump.producer_ids, vec![]);
                assert_eq!(transport_dump.consumer_ids, vec![]);
                assert_eq!(transport_dump.ice_role, transport1.ice_role());
                assert_eq!(&transport_dump.ice_parameters, transport1.ice_parameters());
                assert_eq!(&transport_dump.ice_candidates, transport1.ice_candidates());
                assert_eq!(transport_dump.ice_state, transport1.ice_state());
                assert_eq!(
                    transport_dump.ice_selected_tuple,
                    transport1.ice_selected_tuple(),
                );
                assert_eq!(transport_dump.dtls_parameters, transport1.dtls_parameters());
                assert_eq!(transport_dump.dtls_state, transport1.dtls_state());
                assert_eq!(transport_dump.sctp_parameters, transport1.sctp_parameters());
                assert_eq!(transport_dump.sctp_state, transport1.sctp_state());
            }
        }
    });
}

#[test]
fn create_with_fixed_port_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport({
                let mut options =
                    WebRtcTransportOptions::new(TransportListenIps::new(TransportListenIp {
                        ip: "127.0.0.1".parse().unwrap(),
                        announced_ip: Some("9.9.9.1".parse().unwrap()),
                    }));
                options.port = Some(60_002);

                options
            })
            .await
            .expect("Failed to create WebRTC transport");

        assert_eq!(transport.ice_candidates().get(0).unwrap().port, 60_002);
    });
}

#[test]
fn weak() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        let weak_transport = transport.downgrade();

        assert!(weak_transport.upgrade().is_some());

        drop(transport);

        assert!(weak_transport.upgrade().is_none());
    });
}

#[test]
fn create_non_bindable_ip() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        assert!(matches!(
            router
                .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                    TransportListenIp {
                        ip: "8.8.8.8".parse().unwrap(),
                        announced_ip: None,
                    },
                )))
                .await,
            Err(RequestError::Response { .. }),
        ));
    });
}

#[test]
fn get_stats_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        let stats = transport
            .get_stats()
            .await
            .expect("Failed to get stats on WebRTC transport");

        assert_eq!(stats.len(), 1);
        assert_eq!(stats[0].transport_id, transport.id());
        assert_eq!(stats[0].ice_role, IceRole::Controlled);
        assert_eq!(stats[0].ice_state, IceState::New);
        assert_eq!(stats[0].dtls_state, DtlsState::New);
        assert_eq!(stats[0].sctp_state, None);
        assert_eq!(stats[0].bytes_received, 0);
        assert_eq!(stats[0].recv_bitrate, 0);
        assert_eq!(stats[0].bytes_sent, 0);
        assert_eq!(stats[0].send_bitrate, 0);
        assert_eq!(stats[0].rtp_bytes_received, 0);
        assert_eq!(stats[0].rtp_recv_bitrate, 0);
        assert_eq!(stats[0].rtp_bytes_sent, 0);
        assert_eq!(stats[0].rtp_send_bitrate, 0);
        assert_eq!(stats[0].rtx_bytes_received, 0);
        assert_eq!(stats[0].rtx_recv_bitrate, 0);
        assert_eq!(stats[0].rtx_bytes_sent, 0);
        assert_eq!(stats[0].rtx_send_bitrate, 0);
        assert_eq!(stats[0].probation_bytes_sent, 0);
        assert_eq!(stats[0].probation_send_bitrate, 0);
        assert_eq!(stats[0].ice_selected_tuple, None);
        assert_eq!(stats[0].max_incoming_bitrate, None);
    });
}

#[test]
fn connect_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        let dtls_parameters = DtlsParameters {
            role: DtlsRole::Client,
            fingerprints: vec![DtlsFingerprint::Sha256 {
                value: [
                    0x82, 0x5A, 0x68, 0x3D, 0x36, 0xC3, 0x0A, 0xDE, 0xAF, 0xE7, 0x32, 0x43, 0xD2,
                    0x88, 0x83, 0x57, 0xAC, 0x2D, 0x65, 0xE5, 0x80, 0xC4, 0xB6, 0xFB, 0xAF, 0x1A,
                    0xA0, 0x21, 0x9F, 0x6D, 0x0C, 0xAD,
                ],
            }],
        };

        transport
            .connect(WebRtcTransportRemoteParameters {
                dtls_parameters: dtls_parameters.clone(),
            })
            .await
            .expect("Failed to establish WebRTC transport connection");

        // Must fail if connected.
        assert!(matches!(
            transport
                .connect(WebRtcTransportRemoteParameters { dtls_parameters })
                .await,
            Err(RequestError::Response { .. }),
        ));

        assert_eq!(transport.dtls_parameters().role, DtlsRole::Server);
    });
}

#[test]
fn set_max_incoming_bitrate_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        transport
            .set_max_incoming_bitrate(100000)
            .await
            .expect("Failed to set max incoming bitrate on WebRTC transport");
    });
}

#[test]
fn set_max_outgoing_bitrate_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        transport
            .set_max_outgoing_bitrate(100000)
            .await
            .expect("Failed to set max outgoing bitrate on WebRTC transport");
    });
}

#[test]
fn restart_ice_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        let previous_ice_username_fragment = transport.ice_parameters().username_fragment.clone();
        let previous_ice_password = transport.ice_parameters().password.clone();

        let ice_parameters = transport
            .restart_ice()
            .await
            .expect("Failed to initiate ICE Restart on WebRTC transport");

        assert_ne!(
            ice_parameters.username_fragment,
            previous_ice_username_fragment
        );
        assert_ne!(ice_parameters.password, previous_ice_password);
    });
}

#[test]
fn enable_trace_event_succeeds() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        {
            transport
                .enable_trace_event(vec![TransportTraceEventType::Probation])
                .await
                .expect("Failed to enable trace event");

            let dump = transport
                .dump()
                .await
                .expect("Failed to dump WebRTC transport");

            assert_eq!(dump.trace_event_types.as_str(), "probation");
        }

        {
            transport
                .enable_trace_event(vec![
                    TransportTraceEventType::Probation,
                    TransportTraceEventType::Bwe,
                ])
                .await
                .expect("Failed to enable trace event");

            let dump = transport
                .dump()
                .await
                .expect("Failed to dump WebRTC transport");

            assert_eq!(dump.trace_event_types.as_str(), "probation,bwe");
        }

        {
            transport
                .enable_trace_event(vec![])
                .await
                .expect("Failed to enable trace event");

            let dump = transport
                .dump()
                .await
                .expect("Failed to dump WebRTC transport");

            assert_eq!(dump.trace_event_types.as_str(), "");
        }
    });
}

#[test]
fn close_event() {
    future::block_on(async move {
        let (_worker, router) = init().await;

        let transport = router
            .create_webrtc_transport(WebRtcTransportOptions::new(TransportListenIps::new(
                TransportListenIp {
                    ip: "127.0.0.1".parse().unwrap(),
                    announced_ip: Some("9.9.9.1".parse().unwrap()),
                },
            )))
            .await
            .expect("Failed to create WebRTC transport");

        let (mut close_tx, close_rx) = async_oneshot::oneshot::<()>();
        let _handler = transport.on_close(Box::new(move || {
            let _ = close_tx.send(());
        }));

        drop(transport);

        close_rx.await.expect("Failed to receive close event");
    });
}
