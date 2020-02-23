#include "StdAfx.h"
#include "netease_lyrics_source.h"
#include "curl/curl.h"
#include "picojson/picojson.h"
#include <sstream>

// {195F4508-53B2-4989-AFF5-1733D4B08F68}
const GUID custom_search_requirements::class_guid =
{ 0x195f4508, 0x53b2, 0x4989, { 0xaf, 0xf5, 0x17, 0x33, 0xd4, 0xb0, 0x8f, 0x68} };

static const service_factory_t< custom_search_requirements > custom_search_requirements_factory;

// {98308BDC-87A7-40C5-853E-3A6E6BCF4C2F}
static const GUID guid =
{ 0x98308bdc, 0x87a7, 0x40c5, { 0x85, 0x3e, 0x3a, 0x6e, 0x6b, 0xcf, 0x4c, 0x2f } };

static const source_factory<netease_lyrics_source> my_lyrics_source_factory;

//Each different source must return it's own unique GUID
const GUID netease_lyrics_source::GetGUID()
{
	return guid;
}

bool netease_lyrics_source::PrepareSearch(const search_info* pQuery, lyric_result_client::ptr p_results, search_requirements::ptr& pRequirements)
{
	TRACK_CALL_TEXT("netease_lyrics_source::PrepareSearch");

	//If you need to save some data, use the search requirements interface (see netease_lyrics_source.h).
	//This creates the instance of your custom search requirements.
	custom_search_requirements::ptr requirements = static_api_ptr_t< custom_search_requirements >().get_ptr();

	//Set some data
	requirements->some_example_string = "This is an example string";

	//Copy the requirements so the lyrics plugin can provide them for the Search() function.
	pRequirements = requirements.get_ptr();

	//return true on success, false on failure
	return true;
}

struct curl_receive_t {
	char* buffer;
	int    size;
};

/* Artist structure:
{
	"name": "Fabio Pasquali a.k.a. Jay Lock",
	"id": 30335894,
	"picId": 0,
	"img1v1Id": 0,
	"briefDesc": "",
	"picUrl": "http://p2.music.126.net/6y-UleORITEDbvrOLV0Q8A==/5639395138885805.jpg",
	"img1v1Url": "http://p2.music.126.net/6y-UleORITEDbvrOLV0Q8A==/5639395138885805.jpg",
	"albumSize": 0,
	"alias": [],
	"trans": "",
	"musicSize": 0
}
*/

struct artist_t {
	std::string name;
	uint64_t id;
};

/* Album structure
{
	"name": "Fall In The Dark",
	"id": 74111995,
	"type": "未知",
	"size": 0,
	"picId": 109951163627537840,
	"blurPicUrl": "http://p2.music.126.net/P9gK8gfb6zXO0_Nl-ClGcg==/109951163627537840.jpg",
	"companyId": 0,
	"pic": 109951163627537840,
	"picUrl": "http://p2.music.126.net/P9gK8gfb6zXO0_Nl-ClGcg==/109951163627537840.jpg",
	"publishTime": 1540483200007,
	"description": "",
	"tags": "",
	"company": "Fabio Pasquali a.k.a. Jay Lock",
	"briefDesc": "",
	"artist": { ... },
	"songs": [],
	"alias": [],
	"status": 1,
	"copyrightId": 743010,
	"commentThreadId": "R_AL_3_74111995",
	"artists": [ ... ],
	"picId_str": "109951163627537840"
},
*/

struct album_t {
	std::string name;
	uint64_t id;
	artist_t artist;
	std::string company;
	std::vector<artist_t> artists;
};

/*
Song structure
{
	"name": "Fall In The Dark",
	"id": 1320768247,
	"position": 0,
	"alias": [],
	"status": 0,
	"fee": 8,
	"copyrightId": 743010,
	"disc": "01",
	"no": 1,
	"artists": [ ... ],
	"album": { ... },
	"starred": false,
	"popularity": 5.0,
	"score": 5,
	"starredNum": 0,
	"duration": 199784,
	"playedNum": 0,
	"dayPlays": 0,
	"hearTime": 0,
	"ringtone": "",
	"crbt": null,
	"audition": null,
	"copyFrom": "",
	"commentThreadId": "R_SO_4_1320768247",
	"rtUrl": null,
	"ftype": 0,
	"rtUrls": [],
	"copyright": 1,
	"bMusic": { audiofile_t ... },
	"mp3Url": "http://m2.music.126.net/hmZoNQaqzZALvVp0rE7faA==/0.mp3",
	"mvid": 0,
	"hMusic": { audiofile_t ... },
	"mMusic": { audiofile_t ... },
	"lMusic": { audiofile_t ... },
	"rtype": 0,
	"rurl": null
	},
*/

struct song_t {
	std::string name;
	uint64_t id;
	int no;
	std::vector<artist_t> artists;
	album_t album;
};

/*
Audio file descriptor
{
	"name": null,
	"id": 3523930949,
	"size": 7993513,
	"extension": "mp3",
	"sr": 44100,
	"dfsId": 0,
	"bitrate": 320000,
	"playTime": 199784,
	"volumeDelta": -1.0
}
*/

struct audiofile_t { };

/*
{
    "sgc": false,
    "sfy": false,
    "qfy": false,
    "transUser": {
        "id": 26107975,
        "status": 99,
        "demand": 1,
        "userid": 74828887,
        "nickname": "Zimon字母君",
        "uptime": 1571342030785
    },
    "lrc": {
        "version": 16,
        "lyric": "..."
    },
    "klyric": {
        "version": 0,
        "lyric": null
    },
    "tlyric": {
        "version": 4,
        "lyric": "..."
    },
    "code": 200
}
*/

#define CHECK_ERROR(x) \
	if (!x) goto cleanup

#define parse_member(obj, name, type) \
	CHECK_ERROR(parse_##type(node.get(#name), obj.##name))

#define parse_vec_member(obj, name, type) \
	CHECK_ERROR(parse_vec(node.get(#name), parse_##type, obj.##name))

bool parse_string(picojson::value& obj, std::string& dst) {
	dst.clear();
	if (!obj.is<std::string>()) return false;
	dst = obj.get<std::string>();
	return true;
}

bool parse_uint64(picojson::value& obj, uint64_t& dst) {
	if (!obj.is<double>()) return false;
	dst = obj.get<double>();
	return true;
}

bool parse_int(picojson::value& obj, int& dst) {
	if (!obj.is<double>()) return false;
	dst = obj.get<double>();
	return true;
}

template<typename T, typename F>
bool parse_vec(picojson::value& obj, const F& parser, std::vector<T>& dst) {
	if (!obj.is<picojson::array>()) return false;
	dst.clear();
	T t;
	picojson::array arr = obj.get<picojson::array>();
	for (auto& element : arr) {
		if (parser(element, t)) {
			dst.push_back(t);
		}
	}
	return true;
}

bool parse_artist(picojson::value& node, artist_t& artist) {
	parse_member(artist, name, string);
	parse_member(artist, id, uint64);
	return true;
cleanup:
	return false;
}

bool parse_album(picojson::value& node, album_t& album) {
	parse_member(album, name, string);
	parse_member(album, id, uint64);
	parse_member(album, artist, artist);
	parse_member(album, company, string);
	parse_vec_member(album, artists, artist);
	return true;
cleanup:
	return false;
}

bool parse_song(picojson::value& node, song_t& song) {
	parse_member(song, name, string);
	parse_member(song, id, uint64);
	parse_member(song, no, int);
	parse_vec_member(song, artists, artist);
	parse_member(song, album, album);
	return true;
cleanup:
	return false;
}

std::vector<song_t> parse_search_result(picojson::value& root) {
	std::vector<song_t> ret;
	song_t song;
	picojson::array songs_arr;
	auto songs = root.get("result").get("songs");
	CHECK_ERROR(songs.is<picojson::array>());
	songs_arr = songs.get<picojson::array>();

	for (auto& song_node : songs_arr) {
		if (parse_song(song_node, song)) {
			ret.push_back(song);
		}
	}

cleanup:
	return ret;
}

static size_t curl_receive_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
	size_t realsize = size * nmemb;
	curl_receive_t* mem = reinterpret_cast<curl_receive_t*>(userp);
	LPVOID ptr = realloc(mem->buffer, mem->size + realsize + 1);
	if (ptr == nullptr) {
		return 0;
	}

	mem->buffer = (char*)ptr;
	memcpy(mem->buffer + mem->size, contents, realsize);
	mem->size += realsize;
	((char*)mem->buffer)[mem->size] = 0;
	return realsize;
}

bool str_match(const char* p1, const char* p2) {
	if (!p1 || !p2) return false;
	auto cvt1 = pfc::stringcvt::string_wide_from_utf8(p1);
	auto cvt2 = pfc::stringcvt::string_wide_from_utf8(p2);
	std::wstring s1(cvt1.get_ptr());
	std::wstring s2(cvt2.get_ptr());
	auto l1 = s1.length();
	auto l2 = s2.length();

	// remove garbage

	for (int i = 0; i < l1; ++i) {
		s1[i] = tolower(s1[i]);
		if (iswblank(s1[i]) || iswpunct(s1[i])) {
			s1.erase(i, 1); 
			--i;
			--l1;
		}
	}
	for (int i = 0; i < l2; ++i) {
		s2[i] = tolower(s2[i]);
		if (iswblank(s2[i]) || iswpunct(s2[i])) {
			s2.erase(i, 1);
			--i;
			--l2;
		}
	}

	// edit distance

	std::vector<std::vector<double>> d;
	d.reserve(l1+1);
	for (size_t i = 0; i <= l1; ++i) {
		d.emplace_back(l2+1);
	}
	for (size_t i = 0; i <= max(l1, l2); ++i) {
		if (i <= l1) d[i][0] = i * 0.1;
		if (i <= l2) d[0][i] = i * 0.1;
	}
	for (size_t i = 1; i <= l1; ++i) {
		for (size_t j = 1; j <= l2; ++j) {
			if (s1[i - 1] == s2[j - 1]) { d[i][j] = d[i - 1][j - 1]; }
			else { d[i][j] = min(
				d[i - 1][j - 1] + 1.0, min(
				d[i][j - 1] + 0.1, 
				d[i - 1][j] + 0.1)); }
		}
	}

	double score = d[l1][l2];
	double ratio = score / min(l1, l2) * 100.0;
	bool edit_distance_match = score <= 5.0 && ratio <= 10.0;
	//std::string result = "str_match: score=";
	//result += std::to_string(score);
	//result += " (";
	//result += std::to_string(ratio);
	//result += "%)";
	//console::info(result.c_str());
	//auto cvt3 = pfc::stringcvt::string_utf8_from_wide(s1.c_str());
	//auto cvt4 = pfc::stringcvt::string_utf8_from_wide(s2.c_str());
	//console::info(cvt3.get_ptr());
	//console::info(cvt4.get_ptr());

	// substring match

	bool substring_match = s1.find(s2) != s1.npos || s2.find(s1) != s2.npos;

	return edit_distance_match || substring_match;
}

bool find_match_song(std::vector<song_t>& songs, const search_info* pQuery, song_t &match)
{
	std::stringstream dbg_line;
	dbg_line 
		<< "netease_lyrics_source: search results obtained." << std::endl
		<< "SearchInfo: title = '" << pQuery->title 
		<< "', album = '" << pQuery->album 
		<< "', artist = '" << pQuery->artist 
		<< "'.";
	console::info(dbg_line.str().c_str());

	int id = 0;

	for (auto& song : songs) {
		dbg_line.str("");
		dbg_line << "Result #" << id << ": "
			<< "title = '" << song.name
			<< "', album = '" << song.album.name
			<< "', album artist = '" << song.album.artist.name
			<< "'";
		if (str_match(song.name.c_str(), pQuery->title)
			&& str_match(song.album.name.c_str(), pQuery->album)) {
			match = song;
			dbg_line << " [matched].";
			console::info(dbg_line.str().c_str());
			return true;
		}
		dbg_line << " [not matched].";
		console::info(dbg_line.str().c_str());
		++id;
	}

	return false;
}

bool parse_lyric(picojson::value& root, std::string& text) {
	auto lyric = root.get("lrc").get("lyric");
	if (!lyric.is<std::string>()) {
		console::info("netease_lyrics_source: lyric missing.");
		return false;
	}
	text = lyric.get<std::string>();
	return true;
}

#undef CHECK_ERROR
#define CHECK_ERROR(x) \
	res = (x);\
	if (!res) goto cleanup;

bool netease_lyrics_source::Search(const search_info* pQuery, search_requirements::ptr& pRequirements, lyric_result_client::ptr p_results)
{
	TRACK_CALL_TEXT("netease_lyrics_source::Search");
	bool res;
	CURLcode code;
	CURL* curl = curl_easy_init();
	curl_receive_t mem{nullptr, 0};
	pfc::string8 str_search_in;
	pfc::string8 str_search_query;

	picojson::value json_search_out;
	picojson::value json_lyric_out;
	std::vector<song_t> vec_search_out;
	song_t song_matched;
	std::string str_json_err;
	std::string str_lyric_url;
	std::string str_lyric_out;

	std::string str_debug;

	mem.buffer = (char*)malloc(1);
	if (mem.buffer == nullptr) goto cleanup;
	mem.size = 0;
	if (curl == nullptr) goto cleanup;

	//step 1: search for song id
	str_search_in = pQuery->title;
	str_search_in += " ";
	str_search_in += pQuery->artist;
	str_search_in += " ";
	str_search_in += pQuery->album;

	code = curl_easy_setopt(curl, CURLOPT_URL, "http://music.163.com/api/search/pc");
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "foo_netease_lyrics/1.0");
	CHECK_ERROR(code == CURLE_OK);

	char* str_search_in_escaped = curl_easy_escape(curl, str_search_in.c_str(), str_search_in.length());
	CHECK_ERROR(str_search_in_escaped != nullptr);
	str_search_query = "s=";
	str_search_query += str_search_in_escaped;
	str_search_query += "&limit=8&offset=0&type=1";
	curl_free(str_search_in_escaped);

	code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str_search_query.c_str());
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_receive_callback);
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_perform(curl);
	CHECK_ERROR(code == CURLE_OK);

	str_json_err = picojson::parse(json_search_out, mem.buffer);
	if (!str_json_err.empty()) {
		str_debug = "netease_lyrics_source: search result: malformed json: ";
		str_debug += str_json_err;
		console::error(str_debug.c_str());
	}
	CHECK_ERROR(str_json_err.empty());
	vec_search_out = parse_search_result(json_search_out);
	CHECK_ERROR(find_match_song(vec_search_out, pQuery, song_matched));

	//step 2: issue request to get lyrics
	curl_easy_reset(curl);
	
	str_lyric_url = "http://music.163.com/api/song/lyric?os=pc&id=";
	str_lyric_url += std::to_string(song_matched.id);
	str_lyric_url += "&lv=-1&kv=-1&tv=-1";

	code = curl_easy_setopt(curl, CURLOPT_URL, str_lyric_url.c_str());
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "foo_netease_lyrics/1.0");
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_receive_callback);
	CHECK_ERROR(code == CURLE_OK);

	code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
	CHECK_ERROR(code == CURLE_OK);

	str_debug = "netease_lyrics_source: fetching lyrics at ";
	str_debug += str_lyric_url;
	console::info(str_debug.c_str());

	mem.size = 0;
	code = curl_easy_perform(curl);
	if (code != CURLE_OK) {
		std::string errstr = "netease_lyrics_source: lyrics fetch failed. cURL reports error: ";
		const char* err = curl_easy_strerror(code);
		errstr += err;
		console::error(errstr.c_str());
	}
	CHECK_ERROR(code == CURLE_OK);

	str_json_err = picojson::parse(json_lyric_out, mem.buffer);
	if (!str_json_err.empty()) {
		str_debug = "netease_lyrics_source: lyrics fetch: malformed json: ";
		str_debug += str_json_err;
		console::error(str_debug.c_str());
	}
	CHECK_ERROR(str_json_err.empty());
	CHECK_ERROR(parse_lyric(json_lyric_out, str_lyric_out));

	//then we use the results client to provide an interface which contains the new lyric 
	lyric_container_base* new_lyric = p_results->AddResult();

	//This is only for demonstration purposes, you should should set these to what you search source returns.
	new_lyric->SetFoundInfo(pQuery->artist, pQuery->album, pQuery->title);

	//These tell the user about where the lyric has come from. This is where you should save a web address/file name to allow you to load the lyric
	new_lyric->SetSources("NetEase Music", "Custom Private Source", GetGUID(), ST_INTERNET);

	//Copy the lyric text into here
	new_lyric->SetLyric(str_lyric_out.c_str());

cleanup:
	free(mem.buffer);
	if(curl != nullptr) curl_easy_cleanup(curl);
	return res;
}

bool netease_lyrics_source::Load(lyric_container_base* lyric, request_source p_source)
{
	//Load the lyric

	//This gets the source info you set in Search(), 
	pfc::string8 source, source_private;
	lyric->GetSources(source, source_private);

	return false;

	//You should set this if the lyric loads properly.
	lyric->SetLoaded();

	//return true on success, false on failure
	return true;
}

void netease_lyrics_source::ShowProperties(HWND parent)
{
	//This displays the standard internet source properties.
	static_api_ptr_t< generic_internet_source_properties >()->run(parent);

	return;
}
