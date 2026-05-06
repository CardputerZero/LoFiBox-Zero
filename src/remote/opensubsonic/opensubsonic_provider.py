# SPDX-License-Identifier: GPL-3.0-or-later

import hashlib
import urllib.parse
from typing import Any, Dict, List

from remote.common import media_contract
from remote.tooling.http_json import base_url, json_request


def params(profile: Dict[str, Any]) -> Dict[str, str]:
    username = profile.get("username", "")
    password = profile.get("password", "")
    salt = "lofiboxzero"
    token = hashlib.md5((password + salt).encode("utf-8")).hexdigest()
    return {"u": username, "t": token, "s": salt, "v": "1.16.1", "c": "LoFiBoxZero", "f": "json"}


def profile_schema() -> Dict[str, Any]:
    return {
        "fields": [
            {"name": "base_url", "type": "url", "required": True},
            {"name": "username", "type": "string", "required": True},
            {"name": "password", "type": "secret", "required": True},
        ]
    }


def probe(profile: Dict[str, Any]) -> Dict[str, Any]:
    data = json_request(f"{base_url(profile)}/rest/ping.view?{urllib.parse.urlencode(params(profile))}")
    status = (((data or {}).get("subsonic-response") or {}).get("status"))
    return {
        "available": status == "ok",
        "server_name": profile.get("name", "OpenSubsonic"),
        "user_id": profile.get("username", ""),
        "access_token": "",
        "message": "OK" if status == "ok" else "AUTH_FAILED",
    }


def search(profile: Dict[str, Any], query: str, limit: int) -> List[Dict[str, Any]]:
    request_params = params(profile)
    request_params.update({"query": query, "artistCount": "0", "albumCount": "0", "songCount": str(limit)})
    data = json_request(f"{base_url(profile)}/rest/search3.view?{urllib.parse.urlencode(request_params)}")
    songs = (((data or {}).get("subsonic-response") or {}).get("searchResult3") or {}).get("song") or []
    return [
        media_contract.track(
            profile,
            item.get("id", ""),
            item.get("title", ""),
            item.get("artist", ""),
            item.get("album", ""),
            item.get("parent", ""),
            int(item.get("duration", 0) or 0),
            item.get("coverArt", ""),
        )
        for item in songs
    ]


def node(
    kind: str,
    item_id: str,
    title: str,
    subtitle: str = "",
    playable: bool = False,
    browsable: bool = True,
    artist: str = "",
    album: str = "",
    duration_seconds: int = 0,
    album_id: str = "",
    artwork_key: str = "",
) -> Dict[str, Any]:
    return media_contract.node(
        {},
        kind,
        item_id,
        title,
        subtitle,
        playable,
        browsable,
        artist,
        album,
        duration_seconds,
        album_id,
        artwork_key,
    )


def track_node(item: Dict[str, Any]) -> Dict[str, Any]:
    return node(
        "tracks",
        item.get("id", ""),
        item.get("title", ""),
        item.get("artist", ""),
        True,
        False,
        artist=item.get("artist", ""),
        album=item.get("album", ""),
        duration_seconds=int(item.get("duration", item.get("duration_seconds", 0)) or 0),
        album_id=item.get("parent", item.get("album_id", "")),
        artwork_key=item.get("coverArt", item.get("artwork_key", "")),
    )


def count_label(item: Dict[str, Any]) -> str:
    for key in ("songCount", "childCount", "albumCount", "entryCount"):
        try:
            value = int(item.get(key) or 0)
        except (TypeError, ValueError):
            value = 0
        if value > 0:
            return str(value)
    return ""


def recent(profile: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    request_params = params(profile)
    request_params.update({"type": "newest", "size": str(limit)})
    data = json_request(f"{base_url(profile)}/rest/getAlbumList2.view?{urllib.parse.urlencode(request_params)}")
    albums = (((data or {}).get("subsonic-response") or {}).get("albumList2") or {}).get("album") or []
    tracks: List[Dict[str, Any]] = []
    for album in albums:
        album_id = album.get("id")
        if not album_id:
            continue
        album_data = json_request(f"{base_url(profile)}/rest/getAlbum.view?{urllib.parse.urlencode({**params(profile), 'id': album_id})}")
        songs = (((album_data or {}).get("subsonic-response") or {}).get("album") or {}).get("song") or []
        for item in songs:
            tracks.append(
                media_contract.track(
                    profile,
                    item.get("id", ""),
                    item.get("title", ""),
                    item.get("artist", ""),
                    item.get("album", ""),
                    item.get("parent", ""),
                    int(item.get("duration", 0) or 0),
                    item.get("coverArt", ""),
                )
            )
            if len(tracks) >= limit:
                return tracks
    return tracks


def library_tracks(profile: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    return recent(profile, limit)


def count_items(items: Any) -> int:
    return len(items or []) if isinstance(items, list) else 0


def artist_count(profile: Dict[str, Any]) -> int:
    try:
        data = json_request(f"{base_url(profile)}/rest/getArtists.view?{urllib.parse.urlencode(params(profile))}")
        indexes = (((data or {}).get("subsonic-response") or {}).get("artists") or {}).get("index") or []
        return sum(count_items(index.get("artist") or []) for index in indexes)
    except Exception:
        return 0


def album_count(profile: Dict[str, Any], limit: int) -> int:
    try:
        request_params = params(profile)
        request_params.update({"type": "alphabeticalByName", "size": str(max(limit, 500))})
        data = json_request(f"{base_url(profile)}/rest/getAlbumList2.view?{urllib.parse.urlencode(request_params)}")
        albums = (((data or {}).get("subsonic-response") or {}).get("albumList2") or {}).get("album") or []
        return count_items(albums)
    except Exception:
        return 0


def playlist_count(profile: Dict[str, Any]) -> int:
    try:
        data = json_request(f"{base_url(profile)}/rest/getPlaylists.view?{urllib.parse.urlencode(params(profile))}")
        playlists = (((data or {}).get("subsonic-response") or {}).get("playlists") or {}).get("playlist") or []
        return count_items(playlists)
    except Exception:
        return 0


def starred_count(profile: Dict[str, Any]) -> int:
    try:
        data = json_request(f"{base_url(profile)}/rest/getStarred2.view?{urllib.parse.urlencode(params(profile))}")
        songs = (((data or {}).get("subsonic-response") or {}).get("starred2") or {}).get("song") or []
        return count_items(songs)
    except Exception:
        return 0


def track_count(profile: Dict[str, Any], limit: int) -> int:
    return len(library_tracks(profile, max(limit, 500)))


def browse(profile: Dict[str, Any], parent: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    kind = parent.get("kind", "root")
    if kind in ("root", ""):
        artists = artist_count(profile)
        albums = album_count(profile, limit)
        tracks = track_count(profile, limit)
        playlists = playlist_count(profile)
        favorites = starred_count(profile)
        return [
            node("artists", "artists", "ARTISTS", str(artists)),
            node("albums", "albums", "ALBUMS", str(albums)),
            node("tracks", "tracks", "TRACKS", str(tracks)),
            node("playlists", "playlists", "PLAYLISTS", str(playlists)),
            node("recently-added", "recently-added", "RECENT ADD", str(tracks)),
            node("recently-played", "recently-played", "RECENT PLAY", str(tracks)),
            node("favorites", "favorites", "FAVORITES", str(favorites)),
        ]
    if kind == "artists":
        data = json_request(f"{base_url(profile)}/rest/getArtists.view?{urllib.parse.urlencode(params(profile))}")
        indexes = (((data or {}).get("subsonic-response") or {}).get("artists") or {}).get("index") or []
        result: List[Dict[str, Any]] = []
        for index in indexes:
            for artist in index.get("artist") or []:
                result.append(node("artist", artist.get("id", ""), artist.get("name", "")))
                if len(result) >= limit:
                    return result
        return result
    if kind == "albums":
        request_params = params(profile)
        request_params.update({"type": "alphabeticalByName", "size": str(limit)})
        data = json_request(f"{base_url(profile)}/rest/getAlbumList2.view?{urllib.parse.urlencode(request_params)}")
        albums = (((data or {}).get("subsonic-response") or {}).get("albumList2") or {}).get("album") or []
        return [
            node("album", album.get("id", ""), album.get("name", ""), count_label(album), artist=album.get("artist", ""))
            for album in albums
        ]
    if kind == "playlists":
        data = json_request(f"{base_url(profile)}/rest/getPlaylists.view?{urllib.parse.urlencode(params(profile))}")
        playlists = (((data or {}).get("subsonic-response") or {}).get("playlists") or {}).get("playlist") or []
        return [node("playlist", item.get("id", ""), item.get("name", ""), count_label(item), False, True) for item in playlists[:limit]]
    if kind == "favorites":
        data = json_request(f"{base_url(profile)}/rest/getStarred2.view?{urllib.parse.urlencode(params(profile))}")
        songs = (((data or {}).get("subsonic-response") or {}).get("starred2") or {}).get("song") or []
        return [track_node(item) for item in songs[:limit]]
    if kind == "recently-added":
        return [track_node(item) for item in recent(profile, limit)]
    if kind == "recently-played":
        return [track_node(item) for item in recent(profile, limit)]
    if kind == "album":
        data = json_request(f"{base_url(profile)}/rest/getAlbum.view?{urllib.parse.urlencode({**params(profile), 'id': parent.get('id', '')})}")
        songs = (((data or {}).get("subsonic-response") or {}).get("album") or {}).get("song") or []
        return [track_node(item) for item in songs[:limit]]
    if kind == "playlist":
        data = json_request(f"{base_url(profile)}/rest/getPlaylist.view?{urllib.parse.urlencode({**params(profile), 'id': parent.get('id', '')})}")
        songs = (((data or {}).get("subsonic-response") or {}).get("playlist") or {}).get("entry") or []
        return [track_node(item) for item in songs[:limit]]
    if kind == "artist":
        data = json_request(f"{base_url(profile)}/rest/getArtist.view?{urllib.parse.urlencode({**params(profile), 'id': parent.get('id', '')})}")
        albums = (((data or {}).get("subsonic-response") or {}).get("artist") or {}).get("album") or []
        return [
            node("album", album.get("id", ""), album.get("name", ""), count_label(album), artist=album.get("artist", ""))
            for album in albums[:limit]
        ]
    if kind == "tracks":
        return [track_node(item) for item in library_tracks(profile, limit)]
    return []


def resolve(profile: Dict[str, Any], track: Dict[str, Any]) -> Dict[str, Any]:
    request_params = params(profile)
    request_params["id"] = track["id"]
    return {"url": f"{base_url(profile)}/rest/stream.view?{urllib.parse.urlencode(request_params)}", "headers": [], "seekable": True}
