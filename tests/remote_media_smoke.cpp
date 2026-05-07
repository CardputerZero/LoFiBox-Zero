// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "app/remote_media_contract.h"
#include "app/runtime_services.h"
#include "application/remote_browse_query_service.h"
#include "cache/cache_manager.h"
#include "platform/host/runtime_services_factory.h"
#include "media_fixture_utils.h"

namespace fs = std::filesystem;

namespace {

void setEnvVar(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void configureLocalOnlyHttp()
{
    setEnvVar("NO_PROXY", "127.0.0.1,localhost");
    setEnvVar("no_proxy", "127.0.0.1,localhost");
    setEnvVar("HTTP_PROXY", "");
    setEnvVar("HTTPS_PROXY", "");
    setEnvVar("ALL_PROXY", "");
    setEnvVar("http_proxy", "");
    setEnvVar("https_proxy", "");
    setEnvVar("all_proxy", "");
}

std::optional<int> readServerPort(const fs::path& port_file)
{
    std::ifstream input(port_file);
    int port = 0;
    input >> port;
    if (!input || port <= 0 || port > 65535) {
        return std::nullopt;
    }
    return port;
}

std::optional<int> waitForServerPort(const fs::path& port_file)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto port = readServerPort(port_file)) {
            return port;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    return std::nullopt;
}

bool verifyRemoteGovernance()
{
    lofibox::app::RemoteTrack remote{};
    remote.id = "remote-1";
    remote.title = "Server Title";
    remote.artist = "Server Artist";
    remote.album = "Server Album";
    remote.duration_seconds = 120;

    lofibox::app::TrackMetadata metadata{};
    metadata.title = "Accepted Title";
    metadata.artist = "Accepted Artist";
    metadata.album = "Accepted Album";
    metadata.duration_seconds = 121;

    lofibox::app::TrackLyrics lyrics{};
    lyrics.plain = "accepted lyric";
    lyrics.source = "LRCLIB";

    lofibox::app::TrackIdentity weak{};
    weak.found = true;
    weak.confidence = 0.20;
    weak.source = "MUSICBRAINZ";
    const auto weak_merge = lofibox::app::mergeRemoteGovernedFacts(remote, metadata, lyrics, weak);
    if (weak_merge.title != "Server Title" || weak_merge.artist != "Server Artist" || weak_merge.album != "Server Album") {
        std::cerr << "Weak remote identity must not overwrite plausible server catalog metadata.\n";
        return false;
    }
    if (weak_merge.lyrics_plain != "accepted lyric" || weak_merge.lyrics_source != "LRCLIB") {
        std::cerr << "Lyrics should still be cached as governed local remote facts.\n";
        return false;
    }

    lofibox::app::TrackIdentity strong = weak;
    strong.confidence = 0.94;
    const auto strong_merge = lofibox::app::mergeRemoteGovernedFacts(remote, metadata, lyrics, strong);
    if (strong_merge.title != "Accepted Title"
        || strong_merge.artist != "Accepted Artist"
        || strong_merge.album != "Accepted Album"
        || strong_merge.duration_seconds != 121) {
        std::cerr << "Strong remote identity should govern the local display projection.\n";
        return false;
    }
    return true;
}

class FakeRemoteSourceProvider final : public lofibox::app::RemoteSourceProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "fake-source"; }
    [[nodiscard]] lofibox::app::RemoteSourceSession probe(const lofibox::app::RemoteServerProfile&) const override
    {
        return lofibox::app::RemoteSourceSession{true, "fake", "user", "token", "OK"};
    }
};

class FakeRemoteCatalogProvider final : public lofibox::app::RemoteCatalogProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "fake-catalog"; }
    [[nodiscard]] std::vector<lofibox::app::RemoteTrack> searchTracks(
        const lofibox::app::RemoteServerProfile& profile,
        const lofibox::app::RemoteSourceSession&,
        std::string_view,
        int) const override
    {
        lofibox::app::RemoteTrack track{};
        track.id = "30496";
        track.title = "Bad Bad You, Bad Bad Me";
        track.artist = "Stephen Fretwell";
        track.album = "Magpie";
        track.source_id = profile.id;
        track.source_label = profile.name;
        return {track};
    }

    [[nodiscard]] std::vector<lofibox::app::RemoteTrack> recentTracks(
        const lofibox::app::RemoteServerProfile&,
        const lofibox::app::RemoteSourceSession&,
        int) const override
    {
        return {};
    }
};

bool verifyRemoteCatalogFactsBeatStaleCache(const fs::path& root)
{
    auto services = lofibox::app::withNullRuntimeServices();
    services.remote.remote_source_provider = std::make_shared<FakeRemoteSourceProvider>();
    services.remote.remote_catalog_provider = std::make_shared<FakeRemoteCatalogProvider>();
    services.cache.cache_manager = std::make_shared<lofibox::cache::CacheManager>(root / "cache");

    lofibox::app::RemoteServerProfile profile{
        lofibox::app::RemoteServerKind::Emby,
        "emby-lan",
        "Emby LAN",
        "http://example.invalid",
        "user",
        "",
        ""};

    lofibox::app::RemoteTrack stale{};
    stale.id = "30496";
    stale.title = "Magpie";
    stale.artist = "Stephen Fretwell";
    stale.album = "Magpie";
    stale.source_id = profile.id;
    stale.source_label = profile.name;
    services.cache.cache_manager->putText(
        lofibox::cache::CacheBucket::Metadata,
        lofibox::app::remoteMediaCacheKey(profile, stale.id),
        lofibox::app::serializeRemoteTrackCache(stale));

    auto mutable_profile = profile;
    const auto result = lofibox::application::RemoteBrowseQueryService{services}.search(mutable_profile, 1, "Bad Bad You", 10);
    if (result.tracks.empty() || result.tracks.front().title != "Bad Bad You, Bad Bad Me") {
        std::cerr << "Remote catalog facts must beat stale enrichment cache when provider data is plausible.\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    configureLocalOnlyHttp();

    if (!verifyRemoteGovernance()) {
        return 1;
    }

#if defined(_WIN32)
    const auto python_path = test_media_fixture::resolveExecutablePath(L"PYTHON_EXECUTABLE", L"python.exe");
#elif defined(__linux__)
    const auto python_path = test_media_fixture::resolveExecutablePath("PYTHON_EXECUTABLE", "python3");
#else
    const auto python_path = std::optional<fs::path>{};
#endif
    if (!python_path.has_value()) {
        std::cout << "python unavailable; skipping remote media smoke.\n";
        return 0;
    }

    const fs::path root = fs::temp_directory_path() / "lofibox_zero_remote_media_smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        std::cerr << "Failed to create temp directory.\n";
        return 1;
    }
    if (!verifyRemoteCatalogFactsBeatStaleCache(root)) {
        fs::remove_all(root, ec);
        return 1;
    }

    const fs::path server_script = root / "server.py";
    const fs::path port_file = root / "server.port";
    {
        std::ofstream output(server_script, std::ios::binary | std::ios::trunc);
        output <<
R"(import html
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def _write(self, data):
        payload = json.dumps(data).encode('utf-8')
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def _write_bytes(self, payload, content_type='application/octet-stream'):
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
    def _host(self):
        return self.headers.get('Host') or ('127.0.0.1:' + str(self.server.server_port))

    def do_POST(self):
        if self.path.endswith('/Users/AuthenticateByName'):
            self._write({'AccessToken':'token-123','User':{'Id':'user-1'}})
            return
        if self.path.endswith('/dlna/control'):
            body = self.rfile.read(int(self.headers.get('Content-Length', '0') or 0)).decode('utf-8', errors='replace')
            if '<ObjectID>dlna-song-1</ObjectID>' in body:
                didl = """<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"><item id="dlna-song-1" parentID="folder1"><dc:title>DLNA Song</dc:title><upnp:artist>DLNA Artist</upnp:artist><upnp:album>DLNA Album</upnp:album><res protocolInfo="http-get:*:audio/mpeg:*">http://""" + self._host() + """/media/dlna-song.mp3</res></item></DIDL-Lite>"""
            elif '<ObjectID>folder1</ObjectID>' in body:
                didl = """<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"><item id="dlna-song-1" parentID="folder1"><dc:title>DLNA Song</dc:title><upnp:class>object.item.audioItem.musicTrack</upnp:class><upnp:artist>DLNA Artist</upnp:artist><upnp:album>DLNA Album</upnp:album><res protocolInfo="http-get:*:audio/mpeg:*">http://""" + self._host() + """/media/dlna-song.mp3</res></item><item id="dlna-video-1" parentID="folder1"><dc:title>DLNA Video</dc:title><upnp:class>object.item.videoItem.movie</upnp:class><res protocolInfo="http-get:*:video/mp4:*">http://""" + self._host() + """/media/dlna-video.mp4</res></item></DIDL-Lite>"""
            else:
                didl = """<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/"><container id="folder1" parentID="0" childCount="1"><dc:title>DLNA Folder</dc:title></container></DIDL-Lite>"""
            response = """<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1"><Result>""" + html.escape(didl) + """</Result><NumberReturned>1</NumberReturned><TotalMatches>1</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse></s:Body></s:Envelope>"""
            self._write_bytes(response.encode('utf-8'), 'text/xml')
            return
        self.send_response(404); self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path.endswith('/System/Info/Public'):
            self._write({'ServerName':'TestServer'})
        elif parsed.path.endswith('/Users/user-1/Items'):
            query = parse_qs(parsed.query)
            include_types = query.get('IncludeItemTypes', [''])[0]
            parent_id = query.get('ParentId', [''])[0]
            search_term = query.get('SearchTerm', [''])[0]
            media_types = query.get('MediaTypes', [''])[0]
            if search_term == 'Video Only':
                if media_types == 'Video':
                    self._write({'Items':[{'Id':'vid1','Name':'Video Only Match','Type':'Movie','MediaType':'Video','SeriesName':'Video Series','RunTimeTicks':1200000000,'MediaStreams':[{'Type':'Video','Codec':'h264'}]}]})
                else:
                    self._write({'Items':[]})
                return
            if include_types == 'MusicArtist':
                self._write({'Items':[{'Id':'artist-1','Name':'Artist A','Type':'MusicArtist','MediaType':'Unknown','RecursiveItemCount':1,'ChildCount':0}]})
            elif include_types == 'Audio' and parent_id == 'artist-1':
                self._write({'Items':[{'Id':'trk1','Name':'Song A','Type':'Audio','MediaType':'Audio','Album':'Album A','AlbumId':'alb1','Artists':['Artist A'],'RunTimeTicks':1200000000,'MediaStreams':[{'Type':'Audio','Codec':'flac'}]}]})
            else:
                self._write({'Items':[{'Id':'trk1','Name':'Song A','Type':'Audio','MediaType':'Audio','Album':'Album A','AlbumId':'alb1','Artists':['Artist A'],'RunTimeTicks':1200000000,'MediaStreams':[{'Type':'Audio','Codec':'flac'}]}]})
        elif parsed.path.endswith('/Users/user-1/Items/trk1'):
            self._write({'Id':'trk1','MediaSources':[{'Bitrate':1411000,'Container':'flac','MediaStreams':[{'Type':'Audio','Codec':'flac','SampleRate':44100,'Channels':2}]}]})
        elif parsed.path.endswith('/Users/user-1/Items/Latest'):
            self._write([{'Id':'trk1','Name':'\u00c7\u00fa\u00c4\u00bf 1','Album':'Album A','AlbumId':'alb1','Artists':['\u00ce\u00b4\u00d6\u00aa\u00d2\u00d5\u00ca\u00f5\u00bc\u00d2'],'RunTimeTicks':1200000000}])
        elif parsed.path.endswith('/rest/ping.view'):
            self._write({'subsonic-response':{'status':'ok'}})
        elif parsed.path.endswith('/rest/search3.view'):
            self._write({'subsonic-response':{'searchResult3':{'song':[{'id':'trk2','title':'Song B','artist':'Artist B','album':'Album B','parent':'alb2','duration':123}]}}})
        elif parsed.path.endswith('/rest/getAlbumList2.view'):
            self._write({'subsonic-response':{'albumList2':{'album':[{'id':'alb2','name':'Album B'}]}}})
        elif parsed.path.endswith('/rest/getAlbum.view'):
            self._write({'subsonic-response':{'album':{'song':[{'id':'trk2','title':'Song B','artist':'Artist B','album':'Album B','parent':'alb2','duration':123}]}}})
        elif parsed.path.endswith('/playlist.m3u'):
            text = '#EXTM3U\n#EXTINF:120,Playlist Song\nhttp://' + self._host() + '/media/playlist-song.mp3\n'
            self._write_bytes(text.encode('utf-8'), 'audio/x-mpegurl')
        elif parsed.path.endswith('/dlna/root.xml'):
            text = """<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device><friendlyName>Mock DLNA</friendlyName><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><controlURL>/dlna/control</controlURL></service></serviceList></device></root>"""
            self._write_bytes(text.encode('utf-8'), 'text/xml')
        elif parsed.path.startswith('/media/'):
            self._write_bytes(b'\0' * 32, 'audio/mpeg')
        else:
            self.send_response(404); self.end_headers()

    def do_PROPFIND(self):
        parsed = urlparse(self.path)
        host = self._host()
        if parsed.path.rstrip('/') == '/webdav':
            xml = """<?xml version="1.0"?><d:multistatus xmlns:d="DAV:"><d:response><d:href>/webdav/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response><d:response><d:href>/webdav/Artist/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response></d:multistatus>"""
            self._write_bytes(xml.encode('utf-8'), 'application/xml')
        elif parsed.path.rstrip('/') == '/webdav/Artist':
            xml = """<?xml version="1.0"?><d:multistatus xmlns:d="DAV:"><d:response><d:href>/webdav/Artist/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response><d:response><d:href>/webdav/Artist/Album/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response></d:multistatus>"""
            self._write_bytes(xml.encode('utf-8'), 'application/xml')
        elif parsed.path.rstrip('/') == '/webdav/Artist/Album':
            xml = """<?xml version="1.0"?><d:multistatus xmlns:d="DAV:"><d:response><d:href>/webdav/Artist/Album/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response><d:response><d:href>http://""" + host + """/webdav/Artist/Album/WebDAV%20Song.mp3</d:href><d:propstat><d:prop><d:resourcetype/></d:prop></d:propstat></d:response></d:multistatus>"""
            self._write_bytes(xml.encode('utf-8'), 'application/xml')
        else:
            self.send_response(404); self.end_headers()

server = HTTPServer(('127.0.0.1', 0), Handler)
Path(sys.argv[1]).write_text(str(server.server_port), encoding='utf-8')
server.serve_forever()
)";
    }

    // Keep it simple: start python server detached via OS-specific helper script process.
#if defined(_WIN32)
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring cmd = L"python \"" + server_script.wstring() + L"\" \"" + port_file.wstring() + L"\"";
    if (!CreateProcessW(python_path->wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        std::cerr << "Failed to start mock server.\n";
        fs::remove_all(root, ec);
        return 1;
    }
    CloseHandle(process.hThread);
    const auto cleanup = [&]() {
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 1000);
        CloseHandle(process.hProcess);
        std::error_code cleanup_ec{};
        fs::remove_all(root, cleanup_ec);
    };
#elif defined(__linux__)
    pid_t pid = fork();
    if (pid == 0) {
        execl(python_path->string().c_str(), python_path->string().c_str(), server_script.string().c_str(), port_file.string().c_str(), nullptr);
        _exit(127);
    } else if (pid < 0) {
        std::cerr << "Failed to start mock server.\n";
        fs::remove_all(root, ec);
        return 1;
    }
    const auto cleanup = [&]() {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        std::error_code cleanup_ec{};
        fs::remove_all(root, cleanup_ec);
    };
#endif
    struct ScopeExit {
        std::function<void()> fn{};
        ~ScopeExit()
        {
            if (fn) {
                fn();
            }
        }
    } scope_exit{cleanup};

    const auto port = waitForServerPort(port_file);
    if (!port) {
        std::cerr << "Mock remote media server did not report a listening port.\n";
        return 1;
    }

    auto services = lofibox::platform::host::createHostRuntimeServices();
    if (!services.remote.remote_source_provider->available()
        || !services.remote.remote_catalog_provider->available()
        || !services.remote.remote_stream_resolver->available()) {
        std::cerr << "Expected remote media capabilities to be available.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    const auto jellyfin_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::Jellyfin, "jf", "Jellyfin", "http://127.0.0.1:" + std::to_string(*port), "user", "pass", ""};
    const auto jf_session = services.remote.remote_source_provider->probe(jellyfin_profile);
    if (!jf_session.available || jf_session.access_token.empty()) {
        std::cerr << "Expected Jellyfin probe to succeed.\n";
        return 1;
    }
    const auto jf_tracks = services.remote.remote_catalog_provider->searchTracks(jellyfin_profile, jf_session, "Song", 5);
    if (jf_tracks.empty() || jf_tracks.front().id != "trk1") {
        std::cerr << "Expected Jellyfin search to return a track.\n";
        return 1;
    }
    const auto jf_library = services.remote.remote_catalog_provider->libraryTracks(jellyfin_profile, jf_session, 5);
    if (jf_library.empty() || jf_library.front().id != "trk1") {
        std::cerr << "Expected Jellyfin library catalog to return a track.\n";
        return 1;
    }
    const auto jf_browse_tracks = services.remote.remote_catalog_provider->browse(
        jellyfin_profile,
        jf_session,
        lofibox::app::RemoteCatalogNode{lofibox::app::RemoteCatalogNodeKind::Tracks, "tracks", "TRACKS", "", false, true},
        5);
    if (jf_browse_tracks.empty()
        || jf_browse_tracks.front().artist != "Artist A"
        || jf_browse_tracks.front().album != "Album A"
        || jf_browse_tracks.front().duration_seconds != 120) {
        std::cerr << "Expected Jellyfin browse nodes to preserve artist, album, and duration.\n";
        return 1;
    }
    const auto jf_browse_artists = services.remote.remote_catalog_provider->browse(
        jellyfin_profile,
        jf_session,
        lofibox::app::RemoteCatalogNode{lofibox::app::RemoteCatalogNodeKind::Artists, "artists", "ARTISTS", "", false, true},
        5);
    if (jf_browse_artists.empty()
        || jf_browse_artists.front().kind != lofibox::app::RemoteCatalogNodeKind::Artist
        || jf_browse_artists.front().title != "Artist A"
        || jf_browse_artists.front().subtitle != "1") {
        std::cerr << "Expected Jellyfin artist browse rows to expose normalized song counts.\n";
        return 1;
    }
    const auto jf_artist_tracks = services.remote.remote_catalog_provider->browse(
        jellyfin_profile,
        jf_session,
        jf_browse_artists.front(),
        5);
    if (jf_artist_tracks.empty()
        || !jf_artist_tracks.front().playable
        || jf_artist_tracks.front().title != "Song A") {
        std::cerr << "Expected Jellyfin artist selection to list playable songs, not an empty technical page.\n";
        return 1;
    }
    const auto jf_stream = services.remote.remote_stream_resolver->resolveTrack(jellyfin_profile, jf_session, jf_tracks.front());
    if (!jf_stream
        || jf_stream->url.find("/Audio/trk1/stream") == std::string::npos
        || jf_stream->diagnostics.bitrate_kbps != 1411
        || jf_stream->diagnostics.codec != "flac"
        || jf_stream->diagnostics.sample_rate_hz != 44100
        || jf_stream->diagnostics.channel_count != 2) {
        std::cerr << "Expected Jellyfin resolver to build stream URL.\n";
        return 1;
    }
    const auto jf_recent = services.remote.remote_catalog_provider->recentTracks(jellyfin_profile, jf_session, 5);
    if (jf_recent.empty()
        || jf_recent.front().title != "\xE6\x9B\xB2\xE7\x9B\xAE 1"
        || jf_recent.front().artist != "\xE6\x9C\xAA\xE7\x9F\xA5\xE8\x89\xBA\xE6\x9C\xAF\xE5\xAE\xB6") {
        std::cerr << "Expected remote provider contract to repair mojibake metadata before app consumption.\n";
        return 1;
    }

    const auto subsonic_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::OpenSubsonic, "sub", "Navidrome", "http://127.0.0.1:" + std::to_string(*port), "user", "pass", ""};
    const auto sub_session = services.remote.remote_source_provider->probe(subsonic_profile);
    if (!sub_session.available) {
        std::cerr << "Expected OpenSubsonic probe to succeed.\n";
        return 1;
    }
    const auto sub_tracks = services.remote.remote_catalog_provider->searchTracks(subsonic_profile, sub_session, "Song", 5);
    if (sub_tracks.empty() || sub_tracks.front().id != "trk2") {
        std::cerr << "Expected OpenSubsonic search to return a track.\n";
        return 1;
    }
    const auto sub_library = services.remote.remote_catalog_provider->libraryTracks(subsonic_profile, sub_session, 5);
    if (sub_library.empty() || sub_library.front().id != "trk2") {
        std::cerr << "Expected OpenSubsonic library catalog to return a track.\n";
        return 1;
    }
    const auto sub_browse_tracks = services.remote.remote_catalog_provider->browse(
        subsonic_profile,
        sub_session,
        lofibox::app::RemoteCatalogNode{lofibox::app::RemoteCatalogNodeKind::Tracks, "tracks", "TRACKS", "", false, true},
        5);
    if (sub_browse_tracks.empty()
        || sub_browse_tracks.front().artist != "Artist B"
        || sub_browse_tracks.front().album != "Album B"
        || sub_browse_tracks.front().duration_seconds != 123) {
        std::cerr << "Expected OpenSubsonic browse nodes to preserve artist, album, and duration.\n";
        return 1;
    }
    const auto sub_stream = services.remote.remote_stream_resolver->resolveTrack(subsonic_profile, sub_session, sub_tracks.front());
    if (!sub_stream || sub_stream->url.find("/rest/stream.view") == std::string::npos) {
        std::cerr << "Expected OpenSubsonic resolver to build stream URL.\n";
        return 1;
    }

    const auto navidrome_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::Navidrome, "nav", "Navidrome", "http://127.0.0.1:" + std::to_string(*port), "user", "pass", ""};
    const auto nav_session = services.remote.remote_source_provider->probe(navidrome_profile);
    if (!nav_session.available) {
        std::cerr << "Expected Navidrome probe to use the OpenSubsonic-compatible path.\n";
        return 1;
    }
    const auto nav_tracks = services.remote.remote_catalog_provider->searchTracks(navidrome_profile, nav_session, "Song", 5);
    if (nav_tracks.empty() || nav_tracks.front().id != "trk2") {
        std::cerr << "Expected Navidrome search to return an OpenSubsonic track.\n";
        return 1;
    }

    const auto emby_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::Emby, "emb", "Emby", "http://127.0.0.1:" + std::to_string(*port), "user", "pass", ""};
    const auto emby_session = services.remote.remote_source_provider->probe(emby_profile);
    if (!emby_session.available || emby_session.access_token.empty()) {
        std::cerr << "Expected Emby probe to succeed.\n";
        return 1;
    }
    const auto emby_tracks = services.remote.remote_catalog_provider->searchTracks(emby_profile, emby_session, "Song", 5);
    if (emby_tracks.empty() || emby_tracks.front().id != "trk1") {
        std::cerr << "Expected Emby search to return a track.\n";
        return 1;
    }
    const auto emby_video_only = services.remote.remote_catalog_provider->searchTracks(emby_profile, emby_session, "Video Only", 5);
    if (!emby_video_only.empty()) {
        std::cerr << "Expected Emby video-only search results to be filtered before RemoteTrack projection.\n";
        return 1;
    }
    const auto emby_library = services.remote.remote_catalog_provider->libraryTracks(emby_profile, emby_session, 5);
    if (emby_library.empty() || emby_library.front().id != "trk1") {
        std::cerr << "Expected Emby library catalog to return a track.\n";
        return 1;
    }
    const auto emby_stream = services.remote.remote_stream_resolver->resolveTrack(emby_profile, emby_session, emby_tracks.front());
    if (!emby_stream
        || emby_stream->url.find("/Audio/trk1/stream.mp3") == std::string::npos
        || emby_stream->diagnostics.bitrate_kbps != 1411
        || emby_stream->diagnostics.codec != "flac") {
        std::cerr << "Expected Emby resolver to build stream URL.\n";
        return 1;
    }

    const auto playlist_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::PlaylistManifest, "m3u", "M3U Playlist", "http://127.0.0.1:" + std::to_string(*port) + "/playlist.m3u", "", "", ""};
    const auto playlist_session = services.remote.remote_source_provider->probe(playlist_profile);
    if (!playlist_session.available) {
        std::cerr << "Expected Playlist manifest probe to parse entries.\n";
        return 1;
    }
    const auto playlist_tracks = services.remote.remote_catalog_provider->libraryTracks(playlist_profile, playlist_session, 5);
    if (playlist_tracks.empty() || playlist_tracks.front().title != "Playlist Song") {
        std::cerr << "Expected Playlist manifest provider to expose normalized tracks.\n";
        return 1;
    }
    const auto playlist_stream = services.remote.remote_stream_resolver->resolveTrack(playlist_profile, playlist_session, playlist_tracks.front());
    if (!playlist_stream || playlist_stream->url.find("/media/playlist-song.mp3") == std::string::npos) {
        std::cerr << "Expected Playlist manifest resolver to return the entry URL.\n";
        return 1;
    }

    const auto webdav_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::WebDav, "dav", "WebDAV", "http://127.0.0.1:" + std::to_string(*port) + "/webdav/", "", "", ""};
    const auto webdav_session = services.remote.remote_source_provider->probe(webdav_profile);
    if (!webdav_session.available) {
        std::cerr << "Expected WebDAV probe to list the remote root.\n";
        return 1;
    }
    const auto webdav_library = services.remote.remote_catalog_provider->libraryTracks(webdav_profile, webdav_session, 5);
    if (webdav_library.empty()
        || webdav_library.front().title != "WebDAV Song"
        || webdav_library.front().artist != "Artist"
        || webdav_library.front().album != "Album") {
        std::cerr << "Expected WebDAV provider to recursively expose remote audio files as tracks.\n";
        return 1;
    }
    const auto webdav_stream = services.remote.remote_stream_resolver->resolveTrack(webdav_profile, webdav_session, webdav_library.front());
    if (!webdav_stream || webdav_stream->url.find("/webdav/Artist/Album/WebDAV%20Song.mp3") == std::string::npos) {
        std::cerr << "Expected WebDAV resolver to return the file URL.\n";
        return 1;
    }

    const auto dlna_profile = lofibox::app::RemoteServerProfile{
        lofibox::app::RemoteServerKind::DlnaUpnp, "dlna", "DLNA", "http://127.0.0.1:" + std::to_string(*port) + "/dlna/root.xml", "", "", ""};
    const auto dlna_session = services.remote.remote_source_provider->probe(dlna_profile);
    if (!dlna_session.available) {
        std::cerr << "Expected DLNA probe to find a ContentDirectory service.\n";
        return 1;
    }
    const auto dlna_nodes = services.remote.remote_catalog_provider->browse(
        dlna_profile,
        dlna_session,
        lofibox::app::RemoteCatalogNode{},
        5);
    if (dlna_nodes.empty() || dlna_nodes.front().kind != lofibox::app::RemoteCatalogNodeKind::Folder) {
        std::cerr << "Expected DLNA browse to expose server containers.\n";
        return 1;
    }
    const auto dlna_library = services.remote.remote_catalog_provider->libraryTracks(dlna_profile, dlna_session, 5);
    if (dlna_library.size() != 1U || dlna_library.front().title != "DLNA Song") {
        std::cerr << "Expected DLNA provider to expose audio items as tracks and filter video items.\n";
        return 1;
    }
    const auto dlna_stream = services.remote.remote_stream_resolver->resolveTrack(dlna_profile, dlna_session, dlna_library.front());
    if (!dlna_stream || dlna_stream->url.find("/media/dlna-song.mp3") == std::string::npos) {
        std::cerr << "Expected DLNA resolver to return the item resource URL.\n";
        return 1;
    }

    return 0;
}
