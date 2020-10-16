//========================================================================
//
// Copyright (C) 2020 Matthieu Bruel <Matthieu.Bruel@gmail.com>
//
// This file is a part of ngPost : https://github.com/mbruel/nzbCheck
//
// ngPost is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; version 3.0 of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301,
// USA.
//
//========================================================================

#include "NntpCon.h"
#include "NzbCheck.h"
#include "Nntp.h"
#include <QSslSocket>

NntpCon::NntpCon(NzbCheck *nzbCheck, int id, const NntpServerParams &srvParams)
    : QObject(),
      _nzbCheck(nzbCheck), _id(id), _srvParams(srvParams),
      _socket(nullptr), _isConnected(false),
      _postingState(PostingState::NOT_CONNECTED),
      _currentArticle()
{
    connect(this, &NntpCon::startConnection, this, &NntpCon::onStartConnection, Qt::QueuedConnection);
    connect(this, &NntpCon::killConnection,  this, &NntpCon::onKillConnection,  Qt::QueuedConnection);
}

NntpCon::~NntpCon()
{
    if (_socket)
    {
        disconnect(_socket, &QAbstractSocket::disconnected, this, &NntpCon::onDisconnected);
        disconnect(_socket, &QIODevice::readyRead,          this, &NntpCon::onReadyRead);
        _socket->disconnectFromHost();
        if (_socket->state() != QAbstractSocket::UnconnectedState)
            _socket->waitForDisconnected();
        _socket->deleteLater();
    }
}

void NntpCon::onStartConnection()
{
    if (_srvParams.useSSL)
        _socket = new QSslSocket();
    else
        _socket = new QTcpSocket();

    _socket->setSocketOption(QAbstractSocket::KeepAliveOption, true);
    _socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(_socket, &QAbstractSocket::connected,    this, &NntpCon::onConnected,    Qt::DirectConnection);
    connect(_socket, &QAbstractSocket::disconnected, this, &NntpCon::onDisconnected, Qt::DirectConnection);
    connect(_socket, &QIODevice::readyRead,          this, &NntpCon::onReadyRead,    Qt::DirectConnection);

    qRegisterMetaType<QAbstractSocket::SocketError>("SocketError" );
    connect(_socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onErrors(QAbstractSocket::SocketError)), Qt::DirectConnection);

    _socket->connectToHost(_srvParams.host, _srvParams.port);

#ifdef __USE_CONNECTION_TIMEOUT__
    if (!_timeout)
    {
        _timeout = new QTimer();
        connect(_timeout, &QTimer::timeout, this, &NntpCon::onTimeout);
    }
    _timeout->start(_ngPost->getSocketTimeout());
#endif
}

void NntpCon::onKillConnection()
{
    if (_socket)
    {
        disconnect(_socket, &QIODevice::readyRead,          this, &NntpCon::onReadyRead);
        disconnect(_socket, &QAbstractSocket::disconnected, this, &NntpCon::onDisconnected);
        _socket->disconnectFromHost();
        if (_socket->state() != QAbstractSocket::UnconnectedState)
            _socket->waitForDisconnected();
        _socket->deleteLater();
        _socket = nullptr;
    }
}

void NntpCon::onConnected()
{
    _isConnected = true;
    if (_srvParams.useSSL)
    {
        QSslSocket *sslSock = static_cast<QSslSocket*>(_socket);
        connect(sslSock, SIGNAL(sslErrors(QList<QSslError>)),
                this, SLOT(onSslErrors(QList<QSslError>)), Qt::DirectConnection);

        connect(sslSock, &QSslSocket::encrypted, this, &NntpCon::onEncrypted, Qt::DirectConnection);
        emit sslSock->startClientEncryption();
    }
    else
    {
        if (_nzbCheck->debugMode())
            _nzbCheck->log(tr("[Con #%1] Connected").arg(_id));

        _postingState = PostingState::CONNECTED;
        // We should receive the Hello Message
    }
}

void NntpCon::onEncrypted()
{
    if (_nzbCheck->debugMode())
        _nzbCheck->log(tr("[Con #%1] Connected").arg(_id));

    _postingState = PostingState::CONNECTED;
    // We should receive the Hello Message
}

void NntpCon::onDisconnected()
{
    if (_socket)
    {
        _isConnected    = false;
        _socket->deleteLater();
        _socket = nullptr;
    }
    emit disconnected(this);
}

void NntpCon::onReadyRead()
{
    while (_isConnected && _socket->canReadLine())
    {
        QByteArray line = _socket->readLine();
//        qDebug() << "line: " << line.constData();

        if (_postingState == PostingState::CHECKING_ARTICLE)
        {
            if(strncmp(line.constData(), Nntp::getResponse(430), 3) == 0)
                _nzbCheck->missingArticle(_currentArticle);

            _nzbCheck->articleChecked();
            _postingState = PostingState::IDLE;
            _checkNextArticle();
        }
        else if (_postingState == PostingState::CONNECTED)
        {
            // Check welcome message
            if(strncmp(line.constData(), Nntp::getResponse(200), 3) != 0){
                emit errorConnecting(tr("[Connection #%1] Error connecting to server %2:%3").arg(
                                         _id).arg(_srvParams.host).arg(_srvParams.port));
                _closeConnection();
            }
            else
            {
                // Start authentication : send user info
                if (_srvParams.user.empty())
                {
                    _postingState = PostingState::IDLE;
                    _checkNextArticle();
                }
                else
                {
                    _postingState = PostingState::AUTH_USER;

                    std::string cmd(Nntp::AUTHINFO_USER);
                    cmd += _srvParams.user;
                    cmd += Nntp::ENDLINE;
                    _socket->write(cmd.c_str());
                }
            }
        }
        else if (_postingState == PostingState::AUTH_USER)
        {
            // validate the reply
            if(strncmp(line.constData(), Nntp::getResponse(381), 2) != 0){
                emit errorConnecting(tr("[Connection #%1] Error sending user '%4' to server %2:%3").arg(
                                         _id).arg(_srvParams.host).arg(_srvParams.port).arg(_srvParams.user.c_str()));
                _closeConnection();
            }
            else
            {
                // Continue authentication : send pass info
                _postingState = PostingState::AUTH_PASS;

                std::string cmd(Nntp::AUTHINFO_PASS);
                cmd += _srvParams.pass;
                cmd += Nntp::ENDLINE;
                _socket->write(cmd.c_str());
            }
        }
        else if (_postingState == PostingState::AUTH_PASS)
        {
            if(strncmp(line.constData(), Nntp::getResponse(281), 2) != 0){
                emit errorConnecting(tr("[Connection #%1] Error authentication to server %2:%3 with user '%4' and pass '%5'").arg(
                                         _id).arg(_srvParams.host).arg(_srvParams.port).arg(
                                         _srvParams.user.c_str()).arg(_srvParams.pass.c_str()));
                _closeConnection();
            }
            else
            {
                _postingState = PostingState::IDLE;
                _checkNextArticle();
            }
        }
    }
}

void NntpCon::onSslErrors(const QList<QSslError> &errors)
{
    QString err("Error SSL Socket:\n");
    for(int i = 0 ; i< errors.size() ; ++i)
        err += QString("\t- %1\n").arg(errors[i].errorString());
    _nzbCheck->error(err);
    _closeConnection();

}

void NntpCon::onErrors(QAbstractSocket::SocketError)
{
    _nzbCheck->error(QString("Error Socket: %1").arg(_socket->errorString()));
    _closeConnection();
}

void NntpCon::_closeConnection()
{
    if (_socket && _isConnected)
    {
        disconnect(_socket, &QIODevice::readyRead, this, &NntpCon::onReadyRead);
        _socket->disconnectFromHost();
    }
    else // wrong host info or network down
    {
        if (_socket)
            _socket->deleteLater();
        _socket = nullptr;
        emit disconnected(this);
    }
}

void NntpCon::_checkNextArticle()
{
    _currentArticle = _nzbCheck->getNextArticle();

    if (!_currentArticle.isNull())
    {
        if (_nzbCheck->debugMode())
            _nzbCheck->log(tr("[Con #%1] Checking article %2").arg(_id).arg(_currentArticle));

        _postingState = PostingState::CHECKING_ARTICLE;
        _socket->write(QString("%1 %2\r\n").arg(Nntp::STAT).arg(_currentArticle).toLocal8Bit());
    }
    else
    {
        if (_nzbCheck->debugMode())
            _nzbCheck->log(tr("[Con #%1] No more Article").arg(_id));

        _postingState = PostingState::IDLE;
        _closeConnection();
    }
}
