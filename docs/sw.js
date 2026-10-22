// Service Worker: App startet auch ohne Netz sofort (Daten kommen dann per MQTT).
const CACHE = 'heizung-v1';
const SHELL = ['./', 'index.html', 'manifest.webmanifest', 'icons/icon-180.png', 'icons/icon-192.png', 'icons/icon-512.png'];
const LIBS = ['https://cdnjs.cloudflare.com/ajax/libs/mqtt/5.10.1/mqtt.min.js'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL).then(() => c.addAll(LIBS).catch(() => {}))));
  self.skipWaiting();
});

self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))));
  self.clients.claim();
});

self.addEventListener('fetch', e => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET') return;
  if (url.origin === location.origin) {
    // eigene Dateien: erst Netz (damit Updates ankommen), sonst Cache
    e.respondWith(fetch(e.request).then(r => {
      const copy = r.clone();
      caches.open(CACHE).then(c => c.put(e.request, copy));
      return r;
    }).catch(() => caches.match(e.request, { ignoreSearch: true })));
  } else if (url.pathname.endsWith('mqtt.min.js')) {
    // feste Bibliotheksversion: Cache zuerst
    e.respondWith(caches.match(e.request).then(r => r || fetch(e.request).then(n => {
      const copy = n.clone();
      caches.open(CACHE).then(c => c.put(e.request, copy));
      return n;
    })));
  }
});
