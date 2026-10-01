// ==========================================================================
//  asncache.h — ASN lookup + thread-safe cache (GUI + CLI)
//
//  lookupASN() resolves an IP to its ASN via Team Cymru's DNS-TXT service,
//  skipping private/link-local ranges. AsnCache wraps it with a background
//  thread per lookup and a mutex-guarded cache: no qApp, no Qt event loop,
//  so the exact same cache class works whether the caller is MainWindow
//  (polled by a QTimer) or the CLI report mode (polled by a blocking sleep
//  loop, no QApplication at all).
// ==========================================================================

#pragma once

#include <QtCore/QString>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// Resolve an IP to its ASN via Team Cymru's DNS service. Skips private and
// link-local ranges. Blocking — must be called off the caller's own thread
// if that thread cares about staying responsive (see AsnCache::get()).
QString lookupASN(const QString& ip, bool ipv6);

// True for addresses Team Cymru cannot answer for, so the query is skipped.
bool isUnroutableForAsn(const QString& ip);

// Cached ASN lookups with background resolution. get() returns the cached
// value immediately ("-" while unknown/pending) and, on first request for a
// given IP, kicks off a detached background thread to resolve it — the next
// call for that IP (once resolved) returns the real value.
//
// Copyable/shareable by design: the actual cache lives in a shared_ptr'd
// block, so a background thread started by one copy always has somewhere
// safe to write even if the AsnCache that started it has since gone out of
// scope (mirrors the m_net shared_ptr pattern already used elsewhere).
class AsnCache {
public:
    QString get(const QString& ip, bool ipv6) const;
    // Clears both the resolved cache and the pending set — for a fresh
    // trace whose route may not overlap the previous one at all.
    void    clear();
    // Clears only the pending set, keeping resolved values. Used after the
    // warm-up's ASN/DNS grace window ends: any lookup still in flight at
    // that point keeps running in its detached thread and will still fill
    // the cache when it completes, but dropping its "pending" bookkeeping
    // now means a later get() for that same IP (e.g. the next table
    // refresh) retries instead of being stuck treating it as forever
    // pending.
    void    clearPending();
    bool    hasPending() const;

private:
    struct Shared {
        mutable std::mutex                                mutex;
        std::unordered_map<std::string, QString>          cache;
        std::unordered_set<std::string>                   pending;
    };
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
};
