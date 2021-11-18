# mediasoup v3

mediasoup@3.8


# node

就是如果你要更新你的版本，请在终端输入以下语句：

npm install npm@latest -g


npm install -g npm@6.4.1

node -> version = v11.0.0

win 地址：https://npm.taobao.org/mirrors/node/v10.0.0/


npm install --no-fund




openssl req -new -newkey rsa:1024 -x509 -sha256 -days 3650 -nodes -out fullchain.pem -keyout privkey.pem

。


npm i -f fse@3243.

error
````
npm i -f fsevents@1.2.13
```

[


Linux下mediasoup环境搭建
1.nvm: node版本管理工具

wget -qO- https://raw.githubusercontent.com/creationix/nvm/v0.34.0/install.sh | bash
2.安装node

nvm install v10.16.3
 3.npm更新

npm -g install npm@6.13.4
4.修改npm下载镜像

npm config set registry "http://registry.npm.taobao.org/"
或使用cnpm

npm install -g cnpm --registry=https://registry.npm.taobao.org
5.forever 守护进程

npm install forever -g
6.npm查看当前依赖版本号

npm list mediasoup
7.npm安装指定版本依赖

npm install mediasoup@3.4.8 --> 这个版本
 8.mediasoup-demo 安装参考官方github:

https://github.com/versatica/mediasoup-demo
]

二：使用openssl生成免费证书

1 使用openssl工具生成一个RSA私钥

使用命令：

openssl genrsa -des3 -out server.key 2048
如上：des3 是算法，2048位强度(为了保密性)。 server.key 是密钥文件名 -out的含义是：指生成文件的路径和名称。

如下所示：



我们查看刚刚生成的私钥。使用命令如下：

openssl rsa -text -in server.key
如下图所示：



继续查看 server.key 使用命令：cat server.key,  如下图所示：



2. 创建证书签名请求CSR文件

使用命令如下：

openssl req -new -key server.key -out server.csr
-key的含义是：指定ca私钥
-out的含义是： server.csr 生成证书文件

如下所示：



运行如上命令后，生成CSR时会要求填入以下信息：

复制代码
Country Name (2 letter code) []:CN                        // 输入国家代码，中国填写 CN
State or Province Name (full name) []:HangZhou            // 输入省份，这里填写 HangZhou
Locality Name (eg, city) []:HangZhou                      // 输入城市，我们这里也填写 HangZhou
Organization Name (eg, company) []:tbj                    // 输入组织机构(或公司名，我这里随便写个tbj)
Organizational Unit Name (eg, section) []:tbj             // 输入机构部门
Common Name (eg, fully qualified host name) []:*.abc.com  // 输入域名，我这边是 (*.abc.com)  
Email Address []:tugenhua0707@qq.com                      // 你的邮箱地址

Please enter the following 'extra' attributes
to be sent with your certificate request
A challenge password []:123456                            // 你的证书密码，如果不想设置密码，可以直接回车
复制代码
如上操作后，会在当前目录下生成以下两个文件：

server.key server.csr

如下图所示：



查看csr文件如下命令：

openssl req -text -in server.csr -noout
如下图所示：



3. 生成CA证书

openssl x509 -req -days 365 -in server.csr -signkey server.key -out server.crt
x509的含义: 指定格式
-in的含义: 指定请求文件
-signkey的含义: 自签名

如下图所示：



注意：如上server.crt 是证书持有人的信息，持有人的公钥，以及签署者的签名等信息。

4. 生成客户端证书

生成客户端证书与生成CA证书相似。

4.1. 先要生成私钥

使用命令：

openssl genrsa -out client.key 2048
如下图所示：



4.2 生成请求文件

使用命令：

openssl req -new -key client.key -out client.csr
如下图所示：



4.3 发给ca签名

使用命令： 

openssl x509 -req -days 365 -in client.csr -signkey client.key -out client.crt
如下图所示：










## mediasoup 信令交换流程


客户端通过wss协议登录信令服务后信令服务解析拿到房间roomid查找是否存在房间

1. 不存在房间的操作
   拿到一个worker进程
   创建房间的信息Room ->
					分配一个Router管理房间 
						  设置房间默认声音值
						  创建Bot
									-> 使用mediasoupRouter 创建一个房间传输信息包的最大值512 maxMessageSize
									-> 创建一个DataProducer生产者 -> C++的方法produceData
									
									
									
						
						
						
						










1. 



```
_handleProtooRequest peer = [object Object], request.method = getRouterRtpCapabilitie

_handleProtooRequest peer = [object Object], request.method = createWebRtcTransport +

_handleProtooRequest peer = [object Object], request.method = createWebRtcTransport +

_handleProtooRequest peer = [object Object], request.method = join +6ms
_handleProtooRequest peer = [object Object], request.method = connectWebRtcTransport

_handleProtooRequest peer = [object Object], request.method = connectWebRtcTransport

_handleProtooRequest peer = [object Object], request.method = produce +8ms
_handleProtooRequest peer = [object Object], request.method = produceData +159ms
_handleProtooRequest peer = [object Object], request.method = produceData +3ms
```

















					















