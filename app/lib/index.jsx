import domready from 'domready';
import UrlParse from 'url-parse';
import React from 'react';
import { render } from 'react-dom';
import { Provider } from 'react-redux';
import {
	applyMiddleware as applyReduxMiddleware,
	createStore as createReduxStore
} from 'redux';
import thunk from 'redux-thunk';
// import { createLogger as createReduxLogger } from 'redux-logger';
import randomString from 'random-string';
import * as faceapi from 'face-api.js';
import Logger from './Logger';
import * as utils from './utils';
import randomName from './randomName';
import deviceInfo from './deviceInfo';
import RoomClient from './RoomClient';
import RoomContext from './RoomContext';
import * as cookiesManager from './cookiesManager';
import * as stateActions from './redux/stateActions';
import reducers from './redux/reducers';
import Room from './components/Room';

const logger = new Logger();
const reduxMiddlewares = [ thunk ];

// if (process.env.NODE_ENV === 'development')
// {
// 	const reduxLogger = createReduxLogger(
// 		{
// 			duration  : true,
// 			timestamp : false,
// 			level     : 'log',
// 			logErrors : true
// 		});

// 	reduxMiddlewares.push(reduxLogger);
// }

let roomClient;
const store = createReduxStore(
	reducers,
	undefined,
	applyReduxMiddleware(...reduxMiddlewares)
);

window.STORE = store;

RoomClient.init({ store });

domready(async () =>
{
	logger.debug('DOM ready');

	await utils.initialize();

	run();
});



//function scroll(){
//    //console.log("打印log日志");实时看下效果
//    console.log("开始滚动！");
//   }
// 
//   var scrollFunc = function (e) { 
//    e = e || window.event; 
//    if (e.wheelDelta) { //第一步：先判断浏览器IE，谷歌滑轮事件    
//     if (e.wheelDelta > 0) { //当滑轮向上滚动时 
//      console.log("滑轮向上滚动"); 
//     } 
//     if (e.wheelDelta < 0) { //当滑轮向下滚动时 
//      console.log("滑轮向下滚动"); 
//     } 
//    } else if (e.detail) { //Firefox滑轮事件 
//     if (e.detail> 0) { //当滑轮向上滚动时 
//      console.log("滑轮向上滚动"); 
//     } 
//     if (e.detail< 0) { //当滑轮向下滚动时 
//      console.log("滑轮向下滚动"); 
//     } 
//    } 
//   }
//   //给页面绑定滑轮滚动事件 
//   if (document.addEventListener) {//firefox 
//    document.addEventListener('DOMMouseScroll', scrollFunc, false); 
//   } 
//   //滚动滑轮触发scrollFunc方法 //ie 谷歌 
//   window.onmousewheel = document.onmousewheel = scrollFunc;


async  function scrollFunc(e) 
{ 
    e = e || window.event; 
    if (e.wheelDelta) 
	{ //第一步：先判断浏览器IE，谷歌滑轮事件    
     if (e.wheelDelta > 0) 
	 { //当滑轮向上滚动时 
		action_mouse(4, 0, 0);
      console.log("滑轮向上滚动"); 
     } 
     if (e.wheelDelta < 0) 
	 { //当滑轮向下滚动时 
		action_mouse(5, 0, 0);
      console.log("滑轮向下滚动"); 
     } 
    } 
	else if (e.detail) 
	{ //Firefox滑轮事件 
     if (e.detail> 0) 
	 { //当滑轮向上滚动时 
 action_mouse(4, 0, 0);
      console.log("滑轮向上滚动"); 
     } 
     if (e.detail< 0) 
	 { //当滑轮向下滚动时 
 action_mouse(5, 0, 0);
      console.log("滑轮向下滚动"); 
     } 
    } 
   }


function initMouseMove()
{
 if(!document.all)
 {
  document.captureEvents(Event.MOUSEMOVE);
  //document.captureEvents(Event.CLICK);
 }
 document.onmousemove = mouseMove;
 
 //document.onmouseover = this.mouseMove;  //注册鼠标经过时事件处理函数
//document.onmouseout = this.mouseMove;  //注册鼠标移开时事件处理函数
document.onmousedown = mouseDown;  //注册鼠标按下时事件处理函数
document.onmouseup = mouseUp;  //注册鼠标松开时事件处理函数
// p1.onmousemove = this.mouseMove;  //注册鼠标移动时事件处理函数
document.onclick = mouseClick;  //注册鼠标单击时事件处理函数
document.onmousewheel = scrollFunc;
 //document.ondblclick = this.mouseMove;  //注册鼠标双击时事件处理函数
}




async function  mouseMove(e)
{
	console.log('==========================');
	console.log( e);
	console.log('==========================');
	 var x,y;
	 if(!document.all){
	 
	  x=e.pageX;
	  y=e.pageY;
	 }else{
	  x=document.body.scrollLeft+event.clientX;
	  y=document.body.scrollTop+event.clientY;
	 }
	action_mouse(0, x, y);
}

// 鼠标移动事件
async function  mouseClick(e)
{
	console.log('==========================');
	console.log( e);
	console.log('==========================');
	 var x,y;
	 if(!document.all){
	 
	  x=e.pageX;
	  y=e.pageY;
	 }else{
	  x=document.body.scrollLeft+event.clientX;
	  y=document.body.scrollTop+event.clientY;
	 }
	action_mouse(1, x, y);
}


async function  mouseDown(e)
{
	console.log('==========================');
	console.log( e);
	console.log('==========================');
	 var x,y;
	 if(!document.all){
	 
	  x=e.pageX;
	  y=e.pageY;
	 }else{
	  x=document.body.scrollLeft+event.clientX;
	  y=document.body.scrollTop+event.clientY;
	 }
	action_mouse(2, x, y);
}


async function  mouseUp(e)
{
	console.log('==========================');
	console.log( e);
	console.log('==========================');
	 var x,y;
	 if(!document.all){
	 
	  x=e.pageX;
	  y=e.pageY;
	 }else{
	  x=document.body.scrollLeft+event.clientX;
	  y=document.body.scrollTop+event.clientY;
	 }
	action_mouse(3, x, y);
}

async function action_mouse(action, wight, height)
{
	 var move_xy =
	 {
		 "event" : action,
		 "wight" : wight,
		 "height": height,
		 "windowwidth" : window.screen.width,
		 "windowheight" : window.screen.height
	 };
	// var postion = 'x = ' + x + ', y = ' + y +', wight = '+	 document.body.offsetWidth  + ', height = ' + document.body.offsetHeight;
	 console.log(JSON.stringify(move_xy));
	 //await this.test();
	 roomClient.sendChatMessage(JSON.stringify(move_xy));
	 
	 logger.debug('sendChatMessage() [text:"%s]', move_xy);
}

async function run()
{
	logger.debug('run() [environment:%s]', process.env.NODE_ENV);

	const urlParser = new UrlParse(window.location.href, true);
	const peerId = randomString({ length: 8 }).toLowerCase();
	let roomId = urlParser.query.roomId;
	let displayName =
		urlParser.query.displayName || (cookiesManager.getUser() || {}).displayName;
	const handler = urlParser.query.handler;
	const useSimulcast = urlParser.query.simulcast !== 'false';
	const useSharingSimulcast = urlParser.query.sharingSimulcast !== 'false';
	const forceTcp = urlParser.query.forceTcp === 'true';
	const produce = urlParser.query.produce !== 'false';
	const consume = urlParser.query.consume !== 'false';
	const forceH264 = urlParser.query.forceH264 === 'true';
	const forceVP9 = urlParser.query.forceVP9 === 'true';
	const svc = urlParser.query.svc;
	const datachannel = urlParser.query.datachannel !== 'false';
	const info = urlParser.query.info === 'true';
	const faceDetection = urlParser.query.faceDetection === 'true';
	const externalVideo = urlParser.query.externalVideo === 'true';
	const throttleSecret = urlParser.query.throttleSecret;
	const e2eKey = urlParser.query.e2eKey;

	// Enable face detection on demand.
	if (faceDetection)
		await faceapi.loadTinyFaceDetectorModel('/resources/face-detector-models');

	if (info)
	{
		// eslint-disable-next-line require-atomic-updates
		window.SHOW_INFO = true;
	}

	if (throttleSecret)
	{
		// eslint-disable-next-line require-atomic-updates
		window.NETWORK_THROTTLE_SECRET = throttleSecret;
	}

	if (!roomId)
	{
		roomId = randomString({ length: 8 }).toLowerCase();

		urlParser.query.roomId = roomId;
		window.history.pushState('', '', urlParser.toString());
	}

	// Get the effective/shareable Room URL.
	const roomUrlParser = new UrlParse(window.location.href, true);

	for (const key of Object.keys(roomUrlParser.query))
	{
		// Don't keep some custom params.
		switch (key)
		{
			case 'roomId':
			case 'handler':
			case 'simulcast':
			case 'sharingSimulcast':
			case 'produce':
			case 'consume':
			case 'forceH264':
			case 'forceVP9':
			case 'forceTcp':
			case 'svc':
			case 'datachannel':
			case 'info':
			case 'faceDetection':
			case 'externalVideo':
			case 'throttleSecret':
			case 'e2eKey':
				break;

			default:
				delete roomUrlParser.query[key];
		}
	}
	delete roomUrlParser.hash;

	const roomUrl = roomUrlParser.toString();

	let displayNameSet;

	// If displayName was provided via URL or Cookie, we are done.
	if (displayName)
	{
		displayNameSet = true;
	}
	// Otherwise pick a random name and mark as "not set".
	else
	{
		displayNameSet = false;
		displayName = randomName();
	}

	// Get current device info.
	const device = deviceInfo();

	store.dispatch(
		stateActions.setRoomUrl(roomUrl));

	store.dispatch(
		stateActions.setRoomFaceDetection(faceDetection));

	store.dispatch(
		stateActions.setMe({ peerId, displayName, displayNameSet, device }));

	roomClient = new RoomClient(
		{
			roomId,
			peerId,
			displayName,
			device,
			handlerName : handler,
			useSimulcast,
			useSharingSimulcast,
			forceTcp,
			produce,
			consume,
			forceH264,
			forceVP9,
			svc,
			datachannel,
			externalVideo,
			e2eKey
		});
	
	// NOTE: For debugging.
	// eslint-disable-next-line require-atomic-updates
	window.CLIENT = roomClient;
	// eslint-disable-next-line require-atomic-updates
	window.CC = roomClient;

	render(
		<Provider store={store}>
			<RoomContext.Provider value={roomClient}>
				<Room />
			</RoomContext.Provider>
		</Provider>,
		document.getElementById('mediasoup-demo-app-container')
	);

	initMouseMove();
}






// NOTE: Debugging stuff.

window.__sendSdps = function()
{
	logger.warn('>>> send transport local SDP offer:');
	logger.warn(
		roomClient._sendTransport._handler._pc.localDescription.sdp);

	logger.warn('>>> send transport remote SDP answer:');
	logger.warn(
		roomClient._sendTransport._handler._pc.remoteDescription.sdp);
};

window.__recvSdps = function()
{
	logger.warn('>>> recv transport remote SDP offer:');
	logger.warn(
		roomClient._recvTransport._handler._pc.remoteDescription.sdp);

	logger.warn('>>> recv transport local SDP answer:');
	logger.warn(
		roomClient._recvTransport._handler._pc.localDescription.sdp);
};

let dataChannelTestInterval = null;

window.__startDataChannelTest = function()
{
	let number = 0;

	const buffer = new ArrayBuffer(32);
	const view = new DataView(buffer);

	dataChannelTestInterval = window.setInterval(() =>
	{
		if (window.DP)
		{
			view.setUint32(0, number++);
			roomClient.sendChatMessage(buffer);
		}
	}, 100);
};

window.__stopDataChannelTest = function()
{
	window.clearInterval(dataChannelTestInterval);

	const buffer = new ArrayBuffer(32);
	const view = new DataView(buffer);

	if (window.DP)
	{
		view.setUint32(0, Math.pow(2, 32) - 1);
		window.DP.send(buffer);
	}
};

window.__testSctp = async function({ timeout = 100, bot = false } = {})
{
	let dp;

	if (!bot)
	{
		await window.CLIENT.enableChatDataProducer();

		dp = window.CLIENT._chatDataProducer;
	}
	else
	{
		await window.CLIENT.enableBotDataProducer();

		dp = window.CLIENT._botDataProducer;
	}

	logger.warn(
		'<<< testSctp: DataProducer created [bot:%s, streamId:%d, readyState:%s]',
		bot ? 'true' : 'false',
		dp.sctpStreamParameters.streamId,
		dp.readyState);

	function send()
	{
		dp.send(`I am streamId ${dp.sctpStreamParameters.streamId}`);
	}

	if (dp.readyState === 'open')
	{
		send();
	}
	else
	{
		dp.on('open', () =>
		{
			logger.warn(
				'<<< testSctp: DataChannel open [streamId:%d]',
				dp.sctpStreamParameters.streamId);

			send();
		});
	}

	setTimeout(() => window.__testSctp({ timeout, bot }), timeout);
};

setInterval(() =>
{
	if (window.CLIENT._sendTransport)
	{
		window.H1 = window.CLIENT._sendTransport._handler;
		window.PC1 = window.CLIENT._sendTransport._handler._pc;
		window.DP = window.CLIENT._chatDataProducer;
	}
	else
	{
		delete window.PC1;
		delete window.DP;
	}

	if (window.CLIENT._recvTransport)
	{
		window.H2 = window.CLIENT._recvTransport._handler;
		window.PC2 = window.CLIENT._recvTransport._handler._pc;
	}
	else
	{
		delete window.PC2;
	}
}, 2000);
