// stl includes
#include <stdexcept>

// Qt includes
#include <QRgb>

// flatbuffer includes
#include <flatbufserver/FlatBufferConnection.h>

// flatbuffer FBS
#include "hyperhdr_reply_generated.h"
#include "hyperhdr_request_generated.h"

FlatBufferConnection::FlatBufferConnection(const QString& origin, const QString & address, int priority, bool skipReply)
	: _socket()
	, _origin(origin)
	, _priority(priority)
	, _prevSocketState(QAbstractSocket::UnconnectedState)
	, _log(Logger::getInstance("FLATBUFCONN"))
	, _registered(false)
{
	QStringList parts = address.split(":");
	if (parts.size() != 2)
	{
		throw std::runtime_error(QString("FLATBUFCONNECTION ERROR: Unable to parse address (%1)").arg(address).toStdString());
	}
	_host = parts[0];

	bool ok;
	_port = parts[1].toUShort(&ok);
	if (!ok)
	{
		throw std::runtime_error(QString("FLATBUFCONNECTION ERROR: Unable to parse the port (%1)").arg(parts[1]).toStdString());
	}

	if(!skipReply)
		connect(&_socket, &QTcpSocket::readyRead, this, &FlatBufferConnection::readData, Qt::UniqueConnection);

	// init connect
	Info(_log, "Connecting to HyperHDR: %s:%d", _host.toStdString().c_str(), _port);
	connectToHost();

	// start the connection timer
	_timer.setInterval(5000);

	connect(&_timer, &QTimer::timeout, this, &FlatBufferConnection::connectToHost);
	_timer.start();
}

FlatBufferConnection::~FlatBufferConnection()
{
	_timer.stop();
	_socket.close();
}

void FlatBufferConnection::readData()
{
	_receiveBuffer += _socket.readAll();

	// check if we can read a header
	while(_receiveBuffer.size() >= 4)
	{
		uint32_t messageSize =
			((_receiveBuffer[0]<<24) & 0xFF000000) |
			((_receiveBuffer[1]<<16) & 0x00FF0000) |
			((_receiveBuffer[2]<< 8) & 0x0000FF00) |
			((_receiveBuffer[3]    ) & 0x000000FF);

		// check if we can read a complete message
		if((uint32_t) _receiveBuffer.size() < messageSize + 4) return;

		// extract message only and remove header + msg from buffer :: QByteArray::remove() does not return the removed data
		const QByteArray msg = _receiveBuffer.mid(4, messageSize);
		_receiveBuffer.remove(0, messageSize + 4);

		const uint8_t* msgData = reinterpret_cast<const uint8_t*>(msg.constData());
		flatbuffers::Verifier verifier(msgData, messageSize);

		if (hyperhdrnet::VerifyReplyBuffer(verifier))
		{
			parseReply(hyperhdrnet::GetReply(msgData));
			continue;
		}
		Error(_log, "Unable to parse reply");
	}
}

void FlatBufferConnection::setSkipReply(bool skip)
{
	if(skip)
		disconnect(&_socket, &QTcpSocket::readyRead, 0, 0);
	else
		connect(&_socket, &QTcpSocket::readyRead, this, &FlatBufferConnection::readData, Qt::UniqueConnection);
}

void FlatBufferConnection::setRegister(const QString& origin, int priority)
{
	auto registerReq = hyperhdrnet::CreateRegister(_builder, _builder.CreateString(QSTRING_CSTR(origin)), priority);
	auto req = hyperhdrnet::CreateRequest(_builder, hyperhdrnet::Command_Register, registerReq.Union());

	_builder.Finish(req);
	uint32_t size = _builder.GetSize();
	const uint8_t header[] = {
		uint8_t((size >> 24) & 0xFF),
		uint8_t((size >> 16) & 0xFF),
		uint8_t((size >>  8) & 0xFF),
		uint8_t((size	  ) & 0xFF)};

	// write message
	int count = 0;
	count += _socket.write(reinterpret_cast<const char *>(header), 4);
	count += _socket.write(reinterpret_cast<const char *>(_builder.GetBufferPointer()), size);
	_socket.flush();
	_builder.Clear();
}

void FlatBufferConnection::setColor(const ColorRgb & color, int priority, int duration)
{
	auto colorReq = hyperhdrnet::CreateColor(_builder, (color.red << 16) | (color.green << 8) | color.blue, duration);
	auto req = hyperhdrnet::CreateRequest(_builder, hyperhdrnet::Command_Color, colorReq.Union());

	_builder.Finish(req);
	sendMessage(_builder.GetBufferPointer(), _builder.GetSize());
	_builder.Clear();
}

void FlatBufferConnection::setImage(const Image<ColorRgb> &image)
{
	auto imgData = _builder.CreateVector(reinterpret_cast<const uint8_t*>(image.memptr()), image.size());
	auto rawImg = hyperhdrnet::CreateRawImage(_builder, imgData, image.width(), image.height());
	auto imageReq = hyperhdrnet::CreateImage(_builder, hyperhdrnet::ImageType_RawImage, rawImg.Union(), -1);
	auto req = hyperhdrnet::CreateRequest(_builder,hyperhdrnet::Command_Image,imageReq.Union());

	_builder.Finish(req);
	sendMessage(_builder.GetBufferPointer(), _builder.GetSize());
	_builder.Clear();
}

void FlatBufferConnection::clear(int priority)
{
	auto clearReq = hyperhdrnet::CreateClear(_builder, priority);
	auto req = hyperhdrnet::CreateRequest(_builder,hyperhdrnet::Command_Clear, clearReq.Union());

	_builder.Finish(req);
	sendMessage(_builder.GetBufferPointer(), _builder.GetSize());
	_builder.Clear();
}

void FlatBufferConnection::clearAll()
{
	clear(-1);
}

void FlatBufferConnection::connectToHost()
{
	// try connection only when
	if (_socket.state() == QAbstractSocket::UnconnectedState)
	   _socket.connectToHost(_host, _port);
}

void FlatBufferConnection::sendMessage(const uint8_t* buffer, uint32_t size)
{
	// print out connection message only when state is changed
	if (_socket.state() != _prevSocketState )
	{
		_registered = false;
		switch (_socket.state() )
		{
			case QAbstractSocket::UnconnectedState:
				Info(_log, "No connection to HyperHDR: %s:%d", _host.toStdString().c_str(), _port);
				break;
			case QAbstractSocket::ConnectedState:
				Info(_log, "Connected to HyperHDR: %s:%d", _host.toStdString().c_str(), _port);
				break;
			default:
				Debug(_log, "Connecting to HyperHDR: %s:%d", _host.toStdString().c_str(), _port);
				break;
	  }
	  _prevSocketState = _socket.state();
	}


	if (_socket.state() != QAbstractSocket::ConnectedState)
		return;

	if(!_registered)
	{
		setRegister(_origin, _priority);
		return;
	}

	const uint8_t header[] = {
		uint8_t((size >> 24) & 0xFF),
		uint8_t((size >> 16) & 0xFF),
		uint8_t((size >>  8) & 0xFF),
		uint8_t((size	  ) & 0xFF)};

	// write message
	int count = 0;
	count += _socket.write(reinterpret_cast<const char *>(header), 4);
	count += _socket.write(reinterpret_cast<const char *>(buffer), size);
	_socket.flush();
}

bool FlatBufferConnection::parseReply(const hyperhdrnet::Reply *reply)
{
	if (!reply->error())
	{
		// no error set must be a success or registered or video
		//const auto videoMode =
		reply->video();
		const auto registered = reply->registered();
		/*if (videoMode != -1) {
			// We got a video reply.
			emit setVideoMode(static_cast<VideoMode>(videoMode));
			return true;
		}*/

		 // We got a registered reply.
		if (registered == -1 || registered != _priority)
			_registered = false;
		else
			_registered = true;

		return true;
	}
	else
		throw std::runtime_error(reply->error()->str());

	return false;
}
