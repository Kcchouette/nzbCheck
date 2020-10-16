//========================================================================
//
// Copyright (C) 2019 Matthieu Bruel <Matthieu.Bruel@gmail.com>
//
// This file is a part of ngPost : https://github.com/mbruel/ngPost
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

#ifndef NNTPSERVERPARAMS_H
#define NNTPSERVERPARAMS_H

#include <QString>

struct NntpServerParams{
    QString     host;
    ushort      port;
    bool        auth;
    std::string user; //!< std::string to be coded 1 byte per char
    std::string pass;
    int         nbCons;
    bool        useSSL;
    bool        enabled;

    static const ushort sDefaultPort = 119;
    static const ushort sDefaultSslPort = 563;


    NntpServerParams():
        host(""), port(sDefaultPort), auth(false), user(""),
        pass(""), nbCons(1), useSSL(false), enabled(true)
    {}

    NntpServerParams(const QString & aHost, ushort aPort = sDefaultPort, bool aAuth = false,
                         const std::string &aUser = "", const std::string &aPass = "",
                         int aNbCons = 1, bool aUseSSL = false):
       host(aHost), port(aPort), auth(aAuth), user(aUser),
       pass(aPass), nbCons(aNbCons), useSSL(aUseSSL), enabled(true)
    {}

    ~NntpServerParams() = default;

    NntpServerParams(const NntpServerParams& o) = default;
    NntpServerParams(NntpServerParams&& aParams) = default;

    inline QString str() const;
};


QString NntpServerParams::str() const
{
    if (auth)
        return QString("[%5con%6 on %1:%2@%3:%4 enabled:%7]").arg(user.c_str()).arg(pass.c_str()).arg(
                    host).arg(port).arg(nbCons).arg(useSSL?" SSL":"").arg(enabled);
    else
        return QString("[%3con%4 on %1:%2 enabled:%5]").arg(host).arg(port).arg(nbCons).arg(
                    useSSL?" SSL":"").arg(enabled);
}

#endif // NNTPSERVERPARAMS_H
