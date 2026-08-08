/*#brief Service worker: precaches the app shell from the versioned manifest and serves it stale-while-revalidate.*/
/*#minify*/
// Client half of the delivery story. The device is slow and often asleep, so the browser should not
// wait on it for the shell: cache the shell once, serve it from cache instantly, and refresh in the
// background. That is RFC 5861 stale-while-revalidate applied on the client, matching what
// pc_delivery_swr decides server-side.
//
// The manifest (pc_delivery_sw_manifest) supplies both the file list and a version tag; the cache is
// named after that version, so publishing a new firmware invalidates the old shell exactly once.

var MANIFEST_URL = "/precache.json";
var CACHE_PREFIX = "pc-";

function manifest() {
  // cache: "no-store" - the manifest is the freshness signal, so it must never come from a cache.
  return fetch(MANIFEST_URL, { cache: "no-store" }).then(function (r) {
    if (!r.ok) throw new Error("manifest " + r.status);
    return r.json();
  });
}

function cacheName(m) {
  return CACHE_PREFIX + (m && m.version ? m.version : "0");
}

self.addEventListener("install", function (e) {
  e.waitUntil(
    manifest()
      .then(function (m) {
        return caches.open(cacheName(m)).then(function (c) {
          return c.addAll((m && m.precache) || []);
        });
      })
      // A failed precache must not wedge the worker: it still installs and fills the cache lazily
      // on first fetch. Losing the shell is better than losing the page.
      .catch(function () {})
      .then(function () {
        return self.skipWaiting();
      })
  );
});

self.addEventListener("activate", function (e) {
  e.waitUntil(
    manifest()
      .then(function (m) {
        var keep = cacheName(m);
        return caches.keys().then(function (keys) {
          return Promise.all(
            keys.map(function (k) {
              // Only ever delete our own versioned caches.
              return k.indexOf(CACHE_PREFIX) === 0 && k !== keep ? caches.delete(k) : null;
            })
          );
        });
      })
      .catch(function () {})
      .then(function () {
        return self.clients.claim();
      })
  );
});

self.addEventListener("fetch", function (e) {
  var req = e.request;
  // Only same-origin GETs are ours to cache; anything else goes straight to the network.
  if (req.method !== "GET" || new URL(req.url).origin !== self.location.origin) return;
  // The manifest drives invalidation, so it is never served from cache.
  if (new URL(req.url).pathname === MANIFEST_URL) return;

  e.respondWith(
    caches.match(req).then(function (hit) {
      var net = fetch(req)
        .then(function (res) {
          // Only cache a real, complete response: a 206 is a byte range, not the whole resource.
          if (res && res.ok && res.status === 200) {
            var copy = res.clone();
            caches.keys().then(function (keys) {
              for (var i = 0; i < keys.length; i++) {
                if (keys[i].indexOf(CACHE_PREFIX) === 0) {
                  caches.open(keys[i]).then(function (c) {
                    c.put(req, copy);
                  });
                  break;
                }
              }
            });
          }
          return res;
        })
        .catch(function () {
          return hit;
        });
      // Stale-while-revalidate: hand back the cached copy now, let the refresh land in the cache.
      return hit || net;
    })
  );
});
