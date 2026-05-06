# SPDX-License-Identifier: GPL-3.0-or-later

import json
import urllib.parse
from typing import Any, Dict, List

from remote.common import media_contract
from remote.tooling.http_json import base_url, json_request


VIDEO_ITEM_TYPES = {"movie", "episode", "series", "musicvideo", "trailer", "video"}


def auth_header() -> str:
    try:
        from version import LOFIBOX_VERSION
    except ImportError:
        LOFIBOX_VERSION = "0.0.0-dev"
    return f'MediaBrowser Client="LoFiBox Zero", Device="LoFiBox Zero", DeviceId="lofibox-zero", Version="{LOFIBOX_VERSION}"'


def profile_schema() -> Dict[str, Any]:
    return {
        "fields": [
            {"name": "base_url", "type": "url", "required": True},
            {"name": "username", "type": "string", "required": True},
            {"name": "password", "type": "secret", "required": True},
            {"name": "api_token", "type": "string", "required": False},
        ]
    }


def probe(profile: Dict[str, Any]) -> Dict[str, Any]:
    base = base_url(profile)
    token = profile.get("api_token", "")
    server_name = "Jellyfin"
    if token:
        info = json_request(f"{base}/System/Info/Public")
        server_name = info.get("ServerName", server_name)
        return {"available": True, "server_name": server_name, "user_id": "", "access_token": token, "message": "OK"}

    body = json.dumps({"Username": profile.get("username", ""), "Pw": profile.get("password", "")}).encode("utf-8")
    auth = json_request(
        f"{base}/Users/AuthenticateByName",
        headers={"Content-Type": "application/json", "X-Emby-Authorization": auth_header()},
        method="POST",
        body=body,
    )
    token = auth.get("AccessToken", "")
    user = auth.get("User", {}) or {}
    info = json_request(f"{base}/System/Info/Public")
    server_name = info.get("ServerName", server_name)
    return {
        "available": bool(token),
        "server_name": server_name,
        "user_id": user.get("Id", ""),
        "access_token": token,
        "message": "OK" if token else "AUTH_FAILED",
    }


def is_audio_item(item: Dict[str, Any]) -> bool:
    media_type = str(item.get("MediaType") or "").casefold()
    if media_type:
        return media_type == "audio"

    item_type = str(item.get("Type") or "").casefold()
    if item_type == "audio":
        return True
    if item_type in VIDEO_ITEM_TYPES:
        return False

    streams = item.get("MediaStreams") or []
    if streams:
        stream_types = {str(stream.get("Type") or "").casefold() for stream in streams}
        if "video" in stream_types:
            return False
        return "audio" in stream_types

    media_sources = item.get("MediaSources") or []
    for source in media_sources:
        stream_types = {str(stream.get("Type") or "").casefold() for stream in source.get("MediaStreams") or []}
        if "video" in stream_types:
            return False
        if "audio" in stream_types:
            return True

    return True


def parse_items(items: List[Dict[str, Any]], profile: Dict[str, Any] | None = None) -> List[Dict[str, Any]]:
    tracks = []
    active_profile = profile or {}
    for item in items:
        if not is_audio_item(item):
            continue
        item_id = item.get("Id", "")
        tracks.append(
            media_contract.track(
                active_profile,
                item_id,
                item.get("Name", ""),
                item_artist(item),
                item_album(item),
                item.get("AlbumId", ""),
                int((item.get("RunTimeTicks") or 0) / 10000000),
                media_contract.item_artwork_key(item),
                media_contract.tokenless_item_artwork_url(active_profile, item_id) if active_profile else "",
            )
        )
    return tracks


def first_named(value: Any) -> str:
    if not value:
        return ""
    if isinstance(value, str):
        return media_contract.text(value)
    if isinstance(value, dict):
        return media_contract.text(value.get("Name") or value.get("name") or value.get("Value") or value.get("value") or "")
    if isinstance(value, list):
        for item in value:
            found = first_named(item)
            if found:
                return found
    return ""


def item_artist(item: Dict[str, Any]) -> str:
    for key in (
        "Artists",
        "ArtistItems",
        "AlbumArtists",
        "AlbumArtistItems",
        "Artist",
        "AlbumArtist",
        "SeriesName",
        "ChannelName",
        "SeriesStudio",
    ):
        found = first_named(item.get(key))
        if found:
            return found
    return ""


def item_album(item: Dict[str, Any]) -> str:
    for key in ("Album", "SeriesName", "ParentName", "CollectionName"):
        found = first_named(item.get(key))
        if found:
            return found
    return ""


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
    artwork_url: str = "",
    profile: Dict[str, Any] | None = None,
) -> Dict[str, Any]:
    return media_contract.node(
        profile or {},
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
        artwork_url,
    )


def count_label(item: Dict[str, Any]) -> str:
    for key in ("SongCount", "RecursiveItemCount", "ChildCount", "AlbumCount", "ItemCount"):
        try:
            value = int(item.get(key) or 0)
        except (TypeError, ValueError):
            value = 0
        if value > 0:
            return str(value)
    return ""


def parse_nodes(items: List[Dict[str, Any]], kind: str, playable: bool = False, profile: Dict[str, Any] | None = None) -> List[Dict[str, Any]]:
    nodes: List[Dict[str, Any]] = []
    active_profile = profile or {}
    for item in items:
        if playable and not is_audio_item(item):
            continue
        title = item.get("Name") or item.get("Title") or item.get("Id", "")
        item_id = item.get("Id", "")
        artist = item_artist(item)
        album = item_album(item)
        duration_seconds = int((item.get("RunTimeTicks") or 0) / 10000000)
        if title or item_id:
            nodes.append(
                node(
                    kind,
                    item_id,
                    title,
                    album if playable else count_label(item),
                    playable=playable,
                    browsable=not playable,
                    artist=artist,
                    album=album,
                    duration_seconds=duration_seconds,
                    album_id=item.get("AlbumId", ""),
                    artwork_key=media_contract.item_artwork_key(item),
                    artwork_url=media_contract.tokenless_item_artwork_url(active_profile, item_id) if active_profile else "",
                    profile=active_profile,
                )
            )
    return nodes


def parse_mixed_folder_nodes(items: List[Dict[str, Any]], profile: Dict[str, Any] | None = None) -> List[Dict[str, Any]]:
    nodes: List[Dict[str, Any]] = []
    active_profile = profile or {}
    for item in items:
        item_id = item.get("Id", "")
        title = item.get("Name") or item.get("Title") or item_id
        item_type = item.get("Type") or ""
        media_type = item.get("MediaType") or ""
        artist = item_artist(item)
        album = item_album(item)
        duration_seconds = int((item.get("RunTimeTicks") or 0) / 10000000)
        artwork_key = media_contract.item_artwork_key(item)
        artwork_url = media_contract.tokenless_item_artwork_url(active_profile, item_id) if active_profile else ""

        if media_type.lower() == "audio" or item_type.lower() == "audio":
            nodes.append(
                node(
                    "tracks",
                    item_id,
                    title,
                    artist or album,
                    playable=True,
                    browsable=False,
                    artist=artist,
                    album=album,
                    duration_seconds=duration_seconds,
                    album_id=item.get("AlbumId", ""),
                    artwork_key=artwork_key,
                    artwork_url=artwork_url,
                    profile=active_profile,
                )
            )
            continue

        if bool(item.get("IsFolder")) or item_type in ("Folder", "CollectionFolder", "AggregateFolder", "MusicAlbum", "Playlist"):
            child_kind = "album" if item_type == "MusicAlbum" else ("playlist" if item_type == "Playlist" else "folder")
            nodes.append(
                node(
                    child_kind,
                    item_id,
                    title,
                    item_type or "Folder",
                    playable=False,
                    browsable=True,
                    artist=artist,
                    album=album,
                    duration_seconds=duration_seconds,
                    album_id=item.get("AlbumId", ""),
                    artwork_key=artwork_key,
                    artwork_url=artwork_url,
                    profile=active_profile,
                )
            )
    return nodes


def items_path(user_id: str) -> str:
    return f"/Users/{urllib.parse.quote(user_id)}/Items" if user_id else "/Items"


def latest_path(user_id: str) -> str:
    return f"/Users/{urllib.parse.quote(user_id)}/Items/Latest" if user_id else "/Items/Latest"


def item_detail(profile: Dict[str, Any], session: Dict[str, Any], item_id: str) -> Dict[str, Any]:
    if not item_id:
        return {}
    base = base_url(profile)
    user_id = session.get("user_id", "")
    token = session.get("access_token", "")
    path = f"/Users/{urllib.parse.quote(user_id)}/Items/{urllib.parse.quote(item_id)}" if user_id else f"/Items/{urllib.parse.quote(item_id)}"
    params = urllib.parse.urlencode({"Fields": "MediaSources,MediaStreams", "api_key": token})
    try:
        return json_request(f"{base}{path}?{params}")
    except Exception:
        return {}


def stream_diagnostics(item: Dict[str, Any]) -> Dict[str, Any]:
    media_sources = item.get("MediaSources") or []
    source = media_sources[0] if media_sources else {}
    streams = source.get("MediaStreams") or item.get("MediaStreams") or []
    audio_stream = next((stream for stream in streams if (stream.get("Type") or "").lower() == "audio"), {})
    bitrate = source.get("Bitrate") or audio_stream.get("BitRate") or audio_stream.get("Bitrate") or 0
    codec = audio_stream.get("Codec") or source.get("Container") or ""
    return {
        "bitrate_kbps": int((bitrate or 0) / 1000),
        "codec": str(codec or ""),
        "sample_rate_hz": int(audio_stream.get("SampleRate") or audio_stream.get("SampleRateHz") or 0),
        "channel_count": int(audio_stream.get("Channels") or audio_stream.get("ChannelCount") or 0),
        "live": bool(source.get("IsInfiniteStream") or item.get("IsLive")),
        "connected": True,
        "connection_status": "READY",
    }


def item_query(
    profile: Dict[str, Any],
    session: Dict[str, Any],
    limit: int,
    query: str = "",
    sort_by: str = "SortName",
    sort_order: str = "Ascending",
) -> List[Dict[str, Any]]:
    base = base_url(profile)
    user_id = session.get("user_id", "")
    token = session.get("access_token", "")
    params_dict = {
        "IncludeItemTypes": "Audio",
        "Recursive": "true",
        "Fields": "RunTimeTicks,Album,AlbumId,Artists,ArtistItems,AlbumArtists,AlbumArtist,AlbumPrimaryImageTag,PrimaryImageAspectRatio,MediaSources,Path,SeriesName,ParentName",
        "Limit": str(limit),
        "SortBy": sort_by,
        "SortOrder": sort_order,
        "api_key": token,
    }
    if query:
        params_dict["SearchTerm"] = query
    data = json_request(f"{base}{items_path(user_id)}?{urllib.parse.urlencode(params_dict)}")
    return parse_items(data.get("Items", []), profile)


def search(profile: Dict[str, Any], session: Dict[str, Any], query: str, limit: int) -> List[Dict[str, Any]]:
    return item_query(profile, session, limit, query=query)


def recent(profile: Dict[str, Any], session: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    base = base_url(profile)
    user_id = session.get("user_id", "")
    token = session.get("access_token", "")
    params = urllib.parse.urlencode({"IncludeItemTypes": "Audio", "Limit": str(limit), "api_key": token})
    return parse_items(json_request(f"{base}{latest_path(user_id)}?{params}"), profile)


def library_tracks(profile: Dict[str, Any], session: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    return item_query(profile, session, limit)


def item_count(profile: Dict[str, Any], session: Dict[str, Any], params_dict: Dict[str, str]) -> int:
    base = base_url(profile)
    user_id = session.get("user_id", "")
    token = session.get("access_token", "")
    request_params = {**params_dict, "Limit": "1", "api_key": token}
    try:
        data = json_request(f"{base}{items_path(user_id)}?{urllib.parse.urlencode(request_params)}")
    except Exception:
        return 0
    try:
        return int(data.get("TotalRecordCount", len(data.get("Items", []) or [])) or 0)
    except (TypeError, ValueError):
        return 0


def root_count_nodes(profile: Dict[str, Any], session: Dict[str, Any]) -> List[Dict[str, Any]]:
    def count(params_dict: Dict[str, str]) -> str:
        return str(item_count(profile, session, params_dict))

    return [
        node("artists", "artists", "ARTISTS", count({"IncludeItemTypes": "MusicArtist", "Recursive": "true"}), profile=profile),
        node("albums", "albums", "ALBUMS", count({"IncludeItemTypes": "MusicAlbum", "Recursive": "true"}), profile=profile),
        node("tracks", "tracks", "TRACKS", count({"IncludeItemTypes": "Audio", "Recursive": "true"}), profile=profile),
        node("genres", "genres", "GENRES", count({"IncludeItemTypes": "MusicGenre", "Recursive": "true"}), profile=profile),
        node("playlists", "playlists", "PLAYLISTS", count({"IncludeItemTypes": "Playlist", "Recursive": "true"}), profile=profile),
        node("folders", "folders", "FOLDERS", count({"Recursive": "false"}), profile=profile),
        node("favorites", "favorites", "FAVORITES", count({"IncludeItemTypes": "Audio", "Recursive": "true", "Filters": "IsFavorite"}), profile=profile),
        node("recently-added", "recently-added", "RECENT ADD", count({"IncludeItemTypes": "Audio", "Recursive": "true"}), profile=profile),
        node("recently-played", "recently-played", "RECENT PLAY", count({"IncludeItemTypes": "Audio", "Recursive": "true", "SortBy": "DatePlayed", "SortOrder": "Descending"}), profile=profile),
    ]


def browse(profile: Dict[str, Any], session: Dict[str, Any], parent: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    base = base_url(profile)
    user_id = session.get("user_id", "")
    token = session.get("access_token", "")
    kind = parent.get("kind", "root")
    parent_id = parent.get("id", "")

    if kind in ("root", ""):
        return root_count_nodes(profile, session)

    common = {"Limit": str(limit), "api_key": token}
    if kind == "artists":
        params = urllib.parse.urlencode({**common, "IncludeItemTypes": "MusicArtist", "Recursive": "true", "SortBy": "SortName", "Fields": "ChildCount,RecursiveItemCount,SongCount"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "artist", profile=profile)
    if kind == "albums":
        params = urllib.parse.urlencode({**common, "IncludeItemTypes": "MusicAlbum", "Recursive": "true", "SortBy": "SortName", "Fields": "ChildCount,RecursiveItemCount,SongCount"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "album", profile=profile)
    if kind == "genres":
        params = urllib.parse.urlencode({**common, "IncludeItemTypes": "MusicGenre", "Recursive": "true", "SortBy": "SortName", "Fields": "ChildCount,RecursiveItemCount,SongCount"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "genre", profile=profile)
    if kind == "playlists":
        params = urllib.parse.urlencode({**common, "IncludeItemTypes": "Playlist", "Recursive": "true", "SortBy": "SortName"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "playlist", profile=profile)
    if kind == "folders":
        params = urllib.parse.urlencode({**common, "Recursive": "false", "SortBy": "SortName"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "folder", profile=profile)
    if kind == "favorites":
        params = urllib.parse.urlencode({**common, "IncludeItemTypes": "Audio", "Recursive": "true", "Filters": "IsFavorite", "Fields": "RunTimeTicks,Album,AlbumId,Artists,ArtistItems,AlbumArtists,AlbumArtist,MediaSources,SeriesName,ParentName"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "tracks", playable=True, profile=profile)
    if kind == "recently-added":
        return [
            node(
                "tracks",
                item["id"],
                item["title"],
                item.get("artist", ""),
                playable=True,
                browsable=False,
                artist=item.get("artist", ""),
                album=item.get("album", ""),
                duration_seconds=int(item.get("duration_seconds", 0) or 0),
                album_id=item.get("album_id", ""),
                artwork_key=item.get("artwork_key", ""),
                artwork_url=item.get("artwork_url", ""),
                profile=profile,
            )
            for item in recent(profile, session, limit)
        ]
    if kind == "recently-played":
        params = urllib.parse.urlencode({**common, "IncludeItemTypes": "Audio", "Recursive": "true", "SortBy": "DatePlayed", "SortOrder": "Descending", "Fields": "RunTimeTicks,Album,AlbumId,Artists,ArtistItems,AlbumArtists,AlbumArtist,MediaSources,SeriesName,ParentName"})
        return parse_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), "tracks", playable=True, profile=profile)
    if kind in ("artist", "album", "genre", "playlist"):
        params_dict = {**common, "IncludeItemTypes": "Audio", "Recursive": "true", "Fields": "RunTimeTicks,Album,AlbumId,Artists,ArtistItems,AlbumArtists,AlbumArtist,MediaSources,SeriesName,ParentName"}
        if kind == "album":
            params_dict["ParentId"] = parent_id
        elif kind == "playlist":
            params_dict["ParentId"] = parent_id
        elif kind == "artist" and parent_id:
            params_dict["ParentId"] = parent_id
        elif kind == "genre" and parent_id:
            params_dict["GenreIds"] = parent_id
        else:
            params_dict["SearchTerm"] = parent.get("title", "")
        params = urllib.parse.urlencode(params_dict)
        items = json_request(f"{base}{items_path(user_id)}?{params}").get("Items", [])
        if not items and kind == "artist" and parent_id:
            for artist_key in ("ArtistIds", "AlbumArtistIds"):
                fallback_params = {**common, "IncludeItemTypes": "Audio", "Recursive": "true", "Fields": "RunTimeTicks,Album,AlbumId,Artists,ArtistItems,AlbumArtists,AlbumArtist,MediaSources,SeriesName,ParentName", artist_key: parent_id}
                items = json_request(f"{base}{items_path(user_id)}?{urllib.parse.urlencode(fallback_params)}").get("Items", [])
                if items:
                    break
        if not items and kind in ("artist", "genre"):
            fallback_params = {**common, "IncludeItemTypes": "Audio", "Recursive": "true", "Fields": "RunTimeTicks,Album,AlbumId,Artists,ArtistItems,AlbumArtists,AlbumArtist,MediaSources,SeriesName,ParentName", "SearchTerm": parent.get("title", "")}
            items = json_request(f"{base}{items_path(user_id)}?{urllib.parse.urlencode(fallback_params)}").get("Items", [])
        return parse_nodes(items, "tracks", playable=True, profile=profile)
    if kind == "folder":
        params = urllib.parse.urlencode({**common, "ParentId": parent_id, "Recursive": "false", "Fields": "RunTimeTicks,MediaSources,ImageTags,AlbumPrimaryImageTag,PrimaryImageAspectRatio,Path"})
        return parse_mixed_folder_nodes(json_request(f"{base}{items_path(user_id)}?{params}").get("Items", []), profile=profile)
    if kind == "tracks":
        return [
            node(
                "tracks",
                item["id"],
                item["title"],
                item.get("artist", ""),
                playable=True,
                browsable=False,
                artist=item.get("artist", ""),
                album=item.get("album", ""),
                duration_seconds=int(item.get("duration_seconds", 0) or 0),
                album_id=item.get("album_id", ""),
                artwork_key=item.get("artwork_key", ""),
                artwork_url=item.get("artwork_url", ""),
                profile=profile,
            )
            for item in library_tracks(profile, session, limit)
        ]
    return []


def resolve(profile: Dict[str, Any], session: Dict[str, Any], track: Dict[str, Any]) -> Dict[str, Any]:
    token = session.get("access_token", "")
    detail = item_detail(profile, session, track.get("id", ""))
    return {
        "url": f"{base_url(profile)}/Audio/{track['id']}/stream?static=true&api_key={urllib.parse.quote(token)}",
        "headers": [],
        "seekable": True,
        **stream_diagnostics(detail),
    }
