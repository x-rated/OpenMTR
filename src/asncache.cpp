#include "asncache.h"

#include <QtCore/QStringList>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <QtCore/QProcess>
#endif

#include <algorithm>
#include <thread>

// True for addresses Team Cymru cannot answer for, so the query is skipped.
// Parsed rather than prefix-matched: the old "172." test threw away the whole
// of 172/8 when only 172.16/12 is private, hiding Google and Cloudflare, and
// it missed 100.64/10 in the other direction. inet_pton rather than
// QHostAddress because that lives in Qt6::Network, which this app does not
// link.
bool isUnroutableForAsn(const QString& ip)
{
    const QByteArray raw = ip.toUtf8();

    in_addr v4{};
    if (inet_pton(AF_INET, raw.constData(), &v4) == 1) {
        // Explicit cast: ntohl() returns u_long on Windows, and the MSVC
        // build compiles with /W4 /WX.
        const uint32_t a = static_cast<uint32_t>(ntohl(v4.s_addr));
        return (a & 0xFF000000u) == 0x0A000000u   // 10/8
            || (a & 0xFFF00000u) == 0xAC100000u   // 172.16/12  (NOT all of 172/8)
            || (a & 0xFFFF0000u) == 0xC0A80000u   // 192.168/16
            || (a & 0xFF000000u) == 0x7F000000u   // 127/8 loopback
            || (a & 0xFFFF0000u) == 0xA9FE0000u   // 169.254/16 link-local
            || (a & 0xFFC00000u) == 0x64400000u   // 100.64/10 CGNAT
            || a == 0u;                           // 0.0.0.0
    }

    in6_addr v6{};
    if (inet_pton(AF_INET6, raw.constData(), &v6) == 1) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(&v6);
        if ((b[0] & 0xFE) == 0xFC) return true;                  // fc00::/7 ULA
        if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10 link-local
        for (int i = 0; i < 16; ++i) if (b[i]) return false;
        return true;                                             // ::
    }

    return true;   // not an address we can query for
}

// Resolve an IP to its ASN via Team Cymru's DNS service. Skips private and
// link-local ranges. Blocking — must be called off the UI thread.
QString lookupASN(const QString& ip, bool ipv6)
{
    if (ip.isEmpty() || isUnroutableForAsn(ip))
        return QString();

    QString query;
    if (!ipv6) {
        QStringList parts = ip.split('.');
        if (parts.size() != 4) return QString();
        std::reverse(parts.begin(), parts.end());
        query = parts.join('.') + ".origin.asn.cymru.com";
    } else {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET6;
        hints.ai_flags  = AI_NUMERICHOST;
        if (getaddrinfo(ip.toStdString().c_str(), nullptr, &hints, &res) != 0) return QString();
        auto resGuard = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>(res, freeaddrinfo);
        auto* sa6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
        QString hex;
        for (int b = 0; b < 16; ++b)
            hex += QString("%1").arg(sa6->sin6_addr.s6_addr[b], 2, 16, QChar('0'));
        QString reversed;
        for (int i = 31; i >= 0; --i) { reversed += hex[i]; if (i > 0) reversed += '.'; }
        query = reversed + ".origin6.asn.cymru.com";
    }

#ifdef Q_OS_WIN
    PDNS_RECORD pDnsRecord = nullptr;
    DNS_STATUS status = DnsQuery_W(query.toStdWString().c_str(), DNS_TYPE_TEXT,
        DNS_QUERY_STANDARD, nullptr, &pDnsRecord, nullptr);
    if (status != ERROR_SUCCESS || !pDnsRecord) return QString();

    QString result;
    for (PDNS_RECORD r = pDnsRecord; r; r = r->pNext) {
        if (r->wType == DNS_TYPE_TEXT && r->Data.TXT.dwStringCount > 0) {
            QString txt = QString::fromWCharArray(r->Data.TXT.pStringArray[0]);
            QString asn = txt.split('|').first().trimmed();
            if (!asn.isEmpty() && asn != "0") result = asn;
            break;
        }
    }
    DnsFree(pDnsRecord, DnsFreeRecordList);
    return result;
#else
    // No native DNS TXT API used here (unlike Windows' DnsQuery_W); shell out
    // to the standard `dig` tool instead, which every macOS install ships
    // with.
    QProcess proc;
    proc.start("dig", {"+short", "txt", query});
    if (proc.waitForFinished(2000)) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (out.startsWith('"')) out.remove(0, 1);
        if (out.endsWith('"'))   out.chop(1);
        QString asn = out.split('|').first().trimmed();
        if (!asn.isEmpty() && asn != "0")
            return asn;
    }
    return QString();
#endif
}

// Cached ASN for an IP. Returns '-' immediately; on the first request it
// resolves in the background and fills the cache for next time. The
// background thread writes its result under a plain mutex rather than
// marshalling back through qApp, so this works with no Qt event loop
// running at all (the CLI report mode) as well as from the GUI.
QString AsnCache::get(const QString& ip, bool ipv6) const
{
    auto key = ip.toStdString();
    std::shared_ptr<Shared> shared = m_shared;

    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        auto it = shared->cache.find(key);
        if (it != shared->cache.end())
            return it->second.isEmpty() ? "-" : it->second;
        if (!shared->pending.insert(key).second)
            return "-";   // already resolving
    }

    std::thread([shared, ip, ipv6, key]() {
        QString asn = lookupASN(ip, ipv6);
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->cache[key] = asn;
        shared->pending.erase(key);
    }).detach();

    return "-";
}

void AsnCache::clear()
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->cache.clear();
    m_shared->pending.clear();
}

void AsnCache::clearPending()
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->pending.clear();
}

bool AsnCache::hasPending() const
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    return !m_shared->pending.empty();
}
