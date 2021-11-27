#[cfg(test)]
mod tests;

use crate::data_structures::{AppData, WebRtcMessage};
use crate::messages::{
    DataProducerCloseRequest, DataProducerDumpRequest, DataProducerGetStatsRequest,
    DataProducerInternal, DataProducerSendData, DataProducerSendNotification,
};
use crate::sctp_parameters::SctpStreamParameters;
use crate::transport::Transport;
use crate::uuid_based_wrapper_type;
use crate::worker::{Channel, NotificationError, PayloadChannel, RequestError};
use async_executor::Executor;
use event_listener_primitives::{BagOnce, HandlerId};
use log::{debug, error};
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use std::fmt;
use std::fmt::Debug;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Weak};

uuid_based_wrapper_type!(
    /// [`DataProducer`] identifier.
    DataProducerId
);

/// [`DataProducer`] options.
#[derive(Debug, Clone)]
#[non_exhaustive]
pub struct DataProducerOptions {
    /// DataProducer id (just for
    /// [`Router::pipe_data_producer_to_router`](crate::router::Router::pipe_producer_to_router)
    /// method).
    pub(super) id: Option<DataProducerId>,
    /// SCTP parameters defining how the endpoint is sending the data.
    /// Required if SCTP/DataChannel is used.
    /// Must not be given if the data producer is created on a DirectTransport.
    pub(super) sctp_stream_parameters: Option<SctpStreamParameters>,
    /// A label which can be used to distinguish this DataChannel from others.
    pub label: String,
    /// Name of the sub-protocol used by this DataChannel.
    pub protocol: String,
    /// Custom application data.
    pub app_data: AppData,
}

impl DataProducerOptions {
    #[must_use]
    pub(super) fn new_pipe_transport(
        data_producer_id: DataProducerId,
        sctp_stream_parameters: SctpStreamParameters,
    ) -> Self {
        Self {
            id: Some(data_producer_id),
            sctp_stream_parameters: Some(sctp_stream_parameters),
            label: "".to_string(),
            protocol: "".to_string(),
            app_data: AppData::default(),
        }
    }

    /// Data producer options for non-Direct transport.
    #[must_use]
    pub fn new_sctp(sctp_stream_parameters: SctpStreamParameters) -> Self {
        Self {
            id: None,
            sctp_stream_parameters: Some(sctp_stream_parameters),
            label: "".to_string(),
            protocol: "".to_string(),
            app_data: AppData::default(),
        }
    }

    /// Data producer options for Direct transport.
    #[must_use]
    pub fn new_direct() -> Self {
        Self {
            id: None,
            sctp_stream_parameters: None,
            label: "".to_string(),
            protocol: "".to_string(),
            app_data: AppData::default(),
        }
    }
}

/// Data consumer type.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash, Deserialize, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum DataProducerType {
    /// The endpoint sends messages using the SCTP protocol.
    Sctp,
    /// Messages are sent directly from the Rust process over a direct transport.
    Direct,
}

#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
#[doc(hidden)]
#[non_exhaustive]
pub struct DataProducerDump {
    pub id: DataProducerId,
    pub r#type: DataProducerType,
    pub label: String,
    pub protocol: String,
    pub sctp_stream_parameters: Option<SctpStreamParameters>,
}

/// RTC statistics of the data producer.
#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
#[non_exhaustive]
#[allow(missing_docs)]
pub struct DataProducerStat {
    // `type` field is present in worker, but ignored here
    pub timestamp: u64,
    pub label: String,
    pub protocol: String,
    pub messages_received: usize,
    pub bytes_received: usize,
}

#[derive(Default)]
struct Handlers {
    transport_close: BagOnce<Box<dyn FnOnce() + Send>>,
    close: BagOnce<Box<dyn FnOnce() + Send>>,
}

struct Inner {
    id: DataProducerId,
    r#type: DataProducerType,
    sctp_stream_parameters: Option<SctpStreamParameters>,
    label: String,
    protocol: String,
    direct: bool,
    executor: Arc<Executor<'static>>,
    channel: Channel,
    payload_channel: PayloadChannel,
    handlers: Arc<Handlers>,
    app_data: AppData,
    transport: Box<dyn Transport>,
    closed: AtomicBool,
    _on_transport_close_handler: Mutex<HandlerId>,
}

impl Drop for Inner {
    fn drop(&mut self) {
        debug!("drop()");

        self.close(true);
    }
}

impl Inner {
    fn close(&self, close_request: bool) {
        if !self.closed.swap(true, Ordering::SeqCst) {
            debug!("close()");

            self.handlers.close.call_simple();

            if close_request {
                let channel = self.channel.clone();
                let request = DataProducerCloseRequest {
                    internal: DataProducerInternal {
                        router_id: self.transport.router_id(),
                        transport_id: self.transport.id(),
                        data_producer_id: self.id,
                    },
                };
                let transport = self.transport.clone();
                self.executor
                    .spawn(async move {
                        if let Err(error) = channel.request(request).await {
                            error!("data producer closing failed on drop: {}", error);
                        }

                        drop(transport);
                    })
                    .detach();
            }
        }
    }
}

/// Data producer created on transport other than
/// [`DirectTransport`](crate::direct_transport::DirectTransport).
#[derive(Clone)]
#[must_use = "Data producer will be closed on drop, make sure to keep it around for as long as needed"]
pub struct RegularDataProducer {
    inner: Arc<Inner>,
}

impl fmt::Debug for RegularDataProducer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("RegularDataProducer")
            .field("id", &self.inner.id)
            .field("type", &self.inner.r#type)
            .field("sctp_stream_parameters", &self.inner.sctp_stream_parameters)
            .field("label", &self.inner.label)
            .field("protocol", &self.inner.protocol)
            .field("transport", &self.inner.transport)
            .field("closed", &self.inner.closed)
            .finish()
    }
}

impl From<RegularDataProducer> for DataProducer {
    fn from(producer: RegularDataProducer) -> Self {
        DataProducer::Regular(producer)
    }
}

/// Data producer created on [`DirectTransport`](crate::direct_transport::DirectTransport).
#[derive(Clone)]
#[must_use = "Data producer will be closed on drop, make sure to keep it around for as long as needed"]
pub struct DirectDataProducer {
    inner: Arc<Inner>,
}

impl fmt::Debug for DirectDataProducer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("DirectDataProducer")
            .field("id", &self.inner.id)
            .field("type", &self.inner.r#type)
            .field("sctp_stream_parameters", &self.inner.sctp_stream_parameters)
            .field("label", &self.inner.label)
            .field("protocol", &self.inner.protocol)
            .field("transport", &self.inner.transport)
            .field("closed", &self.inner.closed)
            .finish()
    }
}

impl From<DirectDataProducer> for DataProducer {
    fn from(producer: DirectDataProducer) -> Self {
        DataProducer::Direct(producer)
    }
}

/// A data producer represents an endpoint capable of injecting data messages into a mediasoup
/// [`Router`](crate::router::Router).
///
/// A data producer can use [SCTP](https://tools.ietf.org/html/rfc4960) (AKA DataChannel) to deliver
/// those messages, or can directly send them from the Rust application if the data producer was
/// created on top of a [`DirectTransport`](crate::direct_transport::DirectTransport).
#[derive(Clone)]
#[non_exhaustive]
#[must_use = "Data producer will be closed on drop, make sure to keep it around for as long as needed"]
pub enum DataProducer {
    /// Data producer created on transport other than
    /// [`DirectTransport`](crate::direct_transport::DirectTransport).
    Regular(RegularDataProducer),
    /// Data producer created on [`DirectTransport`](crate::direct_transport::DirectTransport).
    Direct(DirectDataProducer),
}

impl fmt::Debug for DataProducer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self {
            DataProducer::Regular(producer) => f.debug_tuple("Regular").field(&producer).finish(),
            DataProducer::Direct(producer) => f.debug_tuple("Direct").field(&producer).finish(),
        }
    }
}

impl DataProducer {
    #[allow(clippy::too_many_arguments)]
    pub(super) fn new(
        id: DataProducerId,
        r#type: DataProducerType,
        sctp_stream_parameters: Option<SctpStreamParameters>,
        label: String,
        protocol: String,
        executor: Arc<Executor<'static>>,
        channel: Channel,
        payload_channel: PayloadChannel,
        app_data: AppData,
        transport: Box<dyn Transport>,
        direct: bool,
    ) -> Self {
        debug!("new()");

        let handlers = Arc::<Handlers>::default();

        let inner_weak = Arc::<Mutex<Option<Weak<Inner>>>>::default();
        let on_transport_close_handler = transport.on_close({
            let inner_weak = Arc::clone(&inner_weak);

            Box::new(move || {
                if let Some(inner) = inner_weak.lock().as_ref().and_then(Weak::upgrade) {
                    inner.handlers.transport_close.call_simple();
                    inner.close(false);
                }
            })
        });
        let inner = Arc::new(Inner {
            id,
            r#type,
            sctp_stream_parameters,
            label,
            protocol,
            direct,
            executor,
            channel,
            payload_channel,
            handlers,
            app_data,
            transport,
            closed: AtomicBool::new(false),
            _on_transport_close_handler: Mutex::new(on_transport_close_handler),
        });

        inner_weak.lock().replace(Arc::downgrade(&inner));

        if direct {
            Self::Direct(DirectDataProducer { inner })
        } else {
            Self::Regular(RegularDataProducer { inner })
        }
    }

    /// Data producer identifier.
    #[must_use]
    pub fn id(&self) -> DataProducerId {
        self.inner().id
    }

    /// The type of the data producer.
    #[must_use]
    pub fn r#type(&self) -> DataProducerType {
        self.inner().r#type
    }

    /// The SCTP stream parameters (just if the data producer type is `Sctp`).
    #[must_use]
    pub fn sctp_stream_parameters(&self) -> Option<SctpStreamParameters> {
        self.inner().sctp_stream_parameters
    }

    /// The data producer label.
    #[must_use]
    pub fn label(&self) -> &String {
        &self.inner().label
    }

    /// The data producer sub-protocol.
    #[must_use]
    pub fn protocol(&self) -> &String {
        &self.inner().protocol
    }

    /// Custom application data.
    #[must_use]
    pub fn app_data(&self) -> &AppData {
        &self.inner().app_data
    }

    /// Whether the data producer is closed.
    #[must_use]
    pub fn closed(&self) -> bool {
        self.inner().closed.load(Ordering::SeqCst)
    }

    /// Dump DataProducer.
    #[doc(hidden)]
    pub async fn dump(&self) -> Result<DataProducerDump, RequestError> {
        debug!("dump()");

        self.inner()
            .channel
            .request(DataProducerDumpRequest {
                internal: self.get_internal(),
            })
            .await
    }

    /// Returns current statistics of the data producer.
    ///
    /// Check the [RTC Statistics](https://mediasoup.org/documentation/v3/mediasoup/rtc-statistics/)
    /// section for more details (TypeScript-oriented, but concepts apply here as well).
    pub async fn get_stats(&self) -> Result<Vec<DataProducerStat>, RequestError> {
        debug!("get_stats()");

        self.inner()
            .channel
            .request(DataProducerGetStatsRequest {
                internal: self.get_internal(),
            })
            .await
    }

    /// Callback is called when the transport this data producer belongs to is closed for whatever
    /// reason. The producer itself is also closed. A `on_data_producer_close` callback is called on
    /// all its associated consumers.
    pub fn on_transport_close<F: FnOnce() + Send + 'static>(&self, callback: F) -> HandlerId {
        self.inner()
            .handlers
            .transport_close
            .add(Box::new(callback))
    }

    /// Callback is called when the producer is closed for whatever reason.
    ///
    /// NOTE: Callback will be called in place if data producer is already closed.
    pub fn on_close<F: FnOnce() + Send + 'static>(&self, callback: F) -> HandlerId {
        let handler_id = self.inner().handlers.close.add(Box::new(callback));
        if self.inner().closed.load(Ordering::Relaxed) {
            self.inner().handlers.close.call_simple();
        }
        handler_id
    }

    pub(super) fn close(&self) {
        self.inner().close(true);
    }

    /// Downgrade `DataProducer` to [`WeakDataProducer`] instance.
    #[must_use]
    pub fn downgrade(&self) -> WeakDataProducer {
        WeakDataProducer {
            inner: Arc::downgrade(&self.inner()),
        }
    }

    fn inner(&self) -> &Arc<Inner> {
        match self {
            DataProducer::Regular(data_producer) => &data_producer.inner,
            DataProducer::Direct(data_producer) => &data_producer.inner,
        }
    }

    fn get_internal(&self) -> DataProducerInternal {
        DataProducerInternal {
            router_id: self.inner().transport.router_id(),
            transport_id: self.inner().transport.id(),
            data_producer_id: self.inner().id,
        }
    }
}

impl DirectDataProducer {
    /// Sends direct messages from the Rust process.
    pub async fn send(&self, message: WebRtcMessage) -> Result<(), NotificationError> {
        let (ppid, payload) = message.into_ppid_and_payload();

        self.inner
            .payload_channel
            .notify(
                DataProducerSendNotification {
                    internal: DataProducerInternal {
                        router_id: self.inner.transport.router_id(),
                        transport_id: self.inner.transport.id(),
                        data_producer_id: self.inner.id,
                    },
                    data: DataProducerSendData { ppid },
                },
                payload,
            )
            .await
    }
}

/// Same as [`DataProducer`], but will not be closed when dropped.
///
/// Use [`NonClosingDataProducer::into_inner()`] method to get regular [`DataProducer`] instead and
/// restore regular behavior of [`Drop`] implementation.
pub struct NonClosingDataProducer {
    data_producer: DataProducer,
    on_drop: Option<Box<dyn FnOnce(DataProducer) + Send + 'static>>,
}

impl fmt::Debug for NonClosingDataProducer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("NonClosingDataProducer")
            .field("data_producer", &self.data_producer)
            .finish()
    }
}

impl Drop for NonClosingDataProducer {
    fn drop(&mut self) {
        if let Some(on_drop) = self.on_drop.take() {
            on_drop(self.data_producer.clone())
        }
    }
}

impl NonClosingDataProducer {
    /// * `on_drop` - Callback that takes last `Producer` instance and must do something with it to
    ///   prevent dropping and thus closing
    pub(crate) fn new<F: FnOnce(DataProducer) + Send + 'static>(
        data_producer: DataProducer,
        on_drop: F,
    ) -> Self {
        Self {
            data_producer,
            on_drop: Some(Box::new(on_drop)),
        }
    }

    /// Get inner [`DataProducer`] (which will close on drop in contrast to
    /// `NonClosingDataProducer`).
    pub fn into_inner(mut self) -> DataProducer {
        self.on_drop.take();
        self.data_producer.clone()
    }
}

/// [`WeakDataProducer`] doesn't own data producer instance on mediasoup-worker and will not prevent
/// one from being destroyed once last instance of regular [`DataProducer`] is dropped.
///
/// [`WeakDataProducer`] vs [`DataProducer`] is similar to [`Weak`] vs [`Arc`].
#[derive(Clone)]
pub struct WeakDataProducer {
    inner: Weak<Inner>,
}

impl fmt::Debug for WeakDataProducer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("WeakDataProducer").finish()
    }
}

impl WeakDataProducer {
    /// Attempts to upgrade `WeakDataProducer` to [`DataProducer`] if last instance of one wasn't
    /// dropped yet.
    #[must_use]
    pub fn upgrade(&self) -> Option<DataProducer> {
        let inner = self.inner.upgrade()?;

        let data_producer = if inner.direct {
            DataProducer::Direct(DirectDataProducer { inner })
        } else {
            DataProducer::Regular(RegularDataProducer { inner })
        };

        Some(data_producer)
    }
}
