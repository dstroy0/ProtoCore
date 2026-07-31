// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the WebDAV server core (services/file_transfer/webdav): method classification,
// header parsing, XML escaping, and the 207 Multi-Status builder. No FS/sockets.

#include "services/file_transfer/webdav/webdav.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static bool contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != nullptr;
}

// ---------------------------------------------------------------------------
// Method classification
// ---------------------------------------------------------------------------

void test_method_classification()
{
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_OPTIONS, pc_webdav_method("OPTIONS"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_PROPFIND, pc_webdav_method("PROPFIND"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_PROPPATCH, pc_webdav_method("PROPPATCH"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_MKCOL, pc_webdav_method("MKCOL"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_COPY, pc_webdav_method("COPY"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_MOVE, pc_webdav_method("MOVE"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_LOCK, pc_webdav_method("LOCK"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_UNLOCK, pc_webdav_method("UNLOCK"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_PUT, pc_webdav_method("PUT"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_GET, pc_webdav_method("GET"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_DELETE, pc_webdav_method("DELETE"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_UNSUPPORTED, pc_webdav_method("BREW"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_UNSUPPORTED, pc_webdav_method(nullptr));
}

// Fail-closed guards on the pure builders: xml_escape zero-cap + plain-char truncation,
// dest_path null arguments, and the multi-status / proppatch builders returning the input
// length unchanged (ms_entry) or 0 (proppatch) when the output buffer can't hold the element.
void test_webdav_builder_guards()
{
    char out[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_xml_escape(out, 0, "x")); // zero cap
    // A plain (unescaped) run that overruns a tiny buffer stops mid-copy, still NUL-terminated.
    char tiny[4];
    pc_webdav_xml_escape(tiny, sizeof(tiny), "abcdefgh");
    TEST_ASSERT_TRUE(strlen(tiny) < sizeof(tiny));

    TEST_ASSERT_FALSE(pc_webdav_dest_path(nullptr, out, sizeof(out)));  // null destination
    TEST_ASSERT_FALSE(pc_webdav_dest_path("/x", nullptr, sizeof(out))); // null out
    TEST_ASSERT_FALSE(pc_webdav_dest_path("/x", out, 0));               // zero cap

    // ms_entry: an href whose escaped form + fixed markup overruns the internal 512-byte
    // element scratch returns the input length unchanged (atomic: nothing committed).
    char href[256];
    memset(href, 'a', 255);
    href[255] = '\0';
    char buf[4096];
    TEST_ASSERT_EQUAL_size_t(7, pc_webdav_ms_entry(buf, sizeof(buf), 7, href, false, 42, nullptr, "text/plain"));

    // proppatch: a cap that holds the preamble but not the closing envelope -> 0.
    char small[200];
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(small, sizeof(small), "/x", "", 0));
}

// ---------------------------------------------------------------------------
// Depth header
// ---------------------------------------------------------------------------

void test_depth_parsing()
{
    TEST_ASSERT_EQUAL_INT(0, pc_webdav_depth("0", 1));
    TEST_ASSERT_EQUAL_INT(1, pc_webdav_depth("1", 0));
    TEST_ASSERT_EQUAL_INT(PC_DAV_DEPTH_INFINITY, pc_webdav_depth("infinity", 0));
    TEST_ASSERT_EQUAL_INT(1, pc_webdav_depth(nullptr, 1)); // absent -> default
    TEST_ASSERT_EQUAL_INT(7, pc_webdav_depth("bogus", 7)); // unrecognized -> default
}

// ---------------------------------------------------------------------------
// XML escaping
// ---------------------------------------------------------------------------

void test_xml_escape()
{
    char out[128];
    size_t n = pc_webdav_xml_escape(out, sizeof(out), "a&b<c>d\"e'f");
    TEST_ASSERT_EQUAL_STRING("a&amp;b&lt;c&gt;d&quot;e&apos;f", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
}

void test_xml_escape_truncates_safely()
{
    char out[8];
    pc_webdav_xml_escape(out, sizeof(out), "<<<<<<<<"); // each '<' is 4 chars escaped
    TEST_ASSERT_TRUE(strlen(out) < sizeof(out));        // never overruns
    TEST_ASSERT_EQUAL_STRING("&lt;", out);              // one full entity fits
}

// ---------------------------------------------------------------------------
// Destination header
// ---------------------------------------------------------------------------

void test_dest_absolute_uri()
{
    char out[128];
    TEST_ASSERT_TRUE(pc_webdav_dest_path("http://host:8080/dav/new name.txt", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dav/new name.txt", out); // %20 not present here
}

void test_dest_percent_decoded()
{
    char out[128];
    TEST_ASSERT_TRUE(pc_webdav_dest_path("http://h/dav/a%20b%2Fc.txt", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dav/a b/c.txt", out);
}

void test_dest_abs_path()
{
    char out[128];
    TEST_ASSERT_TRUE(pc_webdav_dest_path("/dav/x.txt", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dav/x.txt", out);
}

void test_dest_rejects_malformed()
{
    char out[128];
    TEST_ASSERT_FALSE(pc_webdav_dest_path("dav/x.txt", out, sizeof(out)));       // not absolute
    TEST_ASSERT_FALSE(pc_webdav_dest_path("http://hostonly", out, sizeof(out))); // no path
    TEST_ASSERT_FALSE(pc_webdav_dest_path("/bad%zz", out, sizeof(out)));         // bad escape
}

// ---------------------------------------------------------------------------
// 207 Multi-Status builder
// ---------------------------------------------------------------------------

void test_multistatus_file_and_collection()
{
    char buf[1024];
    size_t len = 0;
    len = pc_webdav_ms_begin(buf, sizeof(buf), len);
    len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/", true, 0, "Mon, 01 Jan 2026 00:00:00 GMT", "");
    len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/readme.txt", false, 42, "Mon, 01 Jan 2026 00:00:00 GMT",
                             "text/plain");
    len = pc_webdav_ms_end(buf, sizeof(buf), len);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), len);

    TEST_ASSERT_TRUE(contains(buf, "<D:multistatus xmlns:D=\"DAV:\">"));
    TEST_ASSERT_TRUE(contains(buf, "<D:href>/dav/</D:href>"));
    TEST_ASSERT_TRUE(contains(buf, "<D:collection/>")); // the directory entry
    TEST_ASSERT_TRUE(contains(buf, "<D:href>/dav/readme.txt</D:href>"));
    TEST_ASSERT_TRUE(contains(buf, "<D:getcontentlength>42</D:getcontentlength>"));
    TEST_ASSERT_TRUE(contains(buf, "<D:getcontenttype>text/plain</D:getcontenttype>"));
    TEST_ASSERT_TRUE(contains(buf, "</D:multistatus>"));

    // A file entry has no <D:collection/>; the collection has no content length.
    const char *file_resp = strstr(buf, "/dav/readme.txt");
    TEST_ASSERT_FALSE(contains(file_resp, "<D:collection/>"));
}

void test_multistatus_escapes_href()
{
    char buf[512];
    size_t len = 0;
    len = pc_webdav_ms_begin(buf, sizeof(buf), len);
    len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/a&b.txt", false, 1, "", "text/plain");
    len = pc_webdav_ms_end(buf, sizeof(buf), len);
    TEST_ASSERT_TRUE(contains(buf, "<D:href>/dav/a&amp;b.txt</D:href>"));
}

void test_multistatus_entry_stops_when_full()
{
    char buf[80]; // only room for the prolog + open tag, not a whole <response>
    size_t len = 0;
    len = pc_webdav_ms_begin(buf, sizeof(buf), len);
    size_t before = len;
    size_t after = pc_webdav_ms_entry(buf, sizeof(buf), before, "/dav/x.txt", false, 1, "", "text/plain");
    TEST_ASSERT_EQUAL_size_t(before, after); // did not fit -> unchanged, no partial element
    TEST_ASSERT_TRUE(strlen(buf) <= sizeof(buf));
}

// ---------------------------------------------------------------------------
// PROPPATCH 207 builder (read-only properties -> 403)
// ---------------------------------------------------------------------------

// Common scaffold checks: well-formed 207 wrapper around a 403 propstat.
static void assert_proppatch_envelope(const char *buf, size_t len)
{
    TEST_ASSERT_EQUAL_size_t(strlen(buf), len);
    TEST_ASSERT_TRUE(contains(buf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
    TEST_ASSERT_TRUE(contains(buf, "<D:multistatus xmlns:D=\"DAV:\">"));
    TEST_ASSERT_TRUE(contains(buf, "<D:status>HTTP/1.1 403 Forbidden</D:status>"));
    TEST_ASSERT_TRUE(contains(buf, "</D:multistatus>"));
}

void test_proppatch_windows_timestamp()
{
    // The PROPPATCH macOS Finder / Windows Explorer send after a PUT.
    const char *body = "<?xml version=\"1.0\"?>\n"
                       "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:Z=\"urn:schemas-microsoft-com:\">"
                       "<D:set><D:prop>"
                       "<Z:Win32LastModifiedTime>Tue, 06 Jan 2026 00:00:00 GMT</Z:Win32LastModifiedTime>"
                       "</D:prop></D:set></D:propertyupdate>";
    char buf[1024];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/file.txt", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:href>/dav/file.txt</D:href>"));
    // property echoed self-closed with its prefix + xmlns intact
    TEST_ASSERT_TRUE(contains(buf, "<Z:Win32LastModifiedTime>") == false); // not as an open tag with content
    TEST_ASSERT_TRUE(contains(buf, "<Z:Win32LastModifiedTime/>"));
    // wrappers must NOT be echoed as properties
    TEST_ASSERT_FALSE(contains(buf, "<D:set/>"));
    TEST_ASSERT_FALSE(contains(buf, "<D:propertyupdate/>"));
}

void test_proppatch_multiple_and_self_closed()
{
    const char *body = "<D:propertyupdate xmlns:D=\"DAV:\"><D:set><D:prop>"
                       "<D:getlastmodified/>"
                       "<author xmlns=\"http://ns.example/\">x</author>"
                       "</D:prop></D:set></D:propertyupdate>";
    char buf[1024];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/d/", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:getlastmodified/>"));
    TEST_ASSERT_TRUE(contains(buf, "<author xmlns=\"http://ns.example/\"/>")); // default-ns prop, value dropped
}

void test_proppatch_remove_block()
{
    const char *body = "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:Z=\"urn:x\">"
                       "<D:remove><D:prop><Z:custom/></D:prop></D:remove></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<Z:custom/>")); // properties in <remove> are refused too
}

void test_proppatch_escapes_href()
{
    const char *body = "<D:propertyupdate xmlns:D=\"DAV:\"><D:set><D:prop><D:displayname/></D:prop></D:set>"
                       "</D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/a&b", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:href>/dav/a&amp;b</D:href>"));
}

void test_proppatch_empty_body_is_valid()
{
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", "", 0);
    assert_proppatch_envelope(buf, len); // still a well-formed 207 with an empty prop
    TEST_ASSERT_TRUE(contains(buf, "<D:prop>"));
}

void test_proppatch_rejects_injection()
{
    // A property tag carrying a stray '<' must not be echoed (no XML injection).
    const char *body = "<D:propertyupdate xmlns:D=\"DAV:\"><D:set><D:prop>"
                       "<evil attr=\"<broken\"/>"
                       "</D:prop></D:set></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_FALSE(contains(buf, "<broken")); // the malformed tag was skipped
}

void test_proppatch_fuzz_bounded()
{
    // Throw random and partial-XML bytes at the scanner: it must always stay in
    // bounds, never emit a partial document, and leave a NUL-terminated buffer.
    uint32_t rng = 0xC0FFEEu;
    char body[128];
    char buf[1024];
    for (int iter = 0; iter < 20000; iter++)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        size_t n = rng % sizeof(body);
        for (size_t i = 0; i < n; i++)
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            // bias toward XML punctuation so the scanner's tag paths are exercised
            const char *alpha = "<>/:= \"abcZD?!prop";
            body[i] = (rng & 3) ? alpha[(rng >> 2) % 18] : (char)(rng & 0xFF);
        }
        size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/x", body, n);
        TEST_ASSERT_TRUE(len <= sizeof(buf));
        if (len)
        {
            TEST_ASSERT_EQUAL_size_t(len, strlen(buf));
            TEST_ASSERT_TRUE(contains(buf, "</D:multistatus>")); // complete document or len 0
        }
        else
        {
            TEST_ASSERT_EQUAL_size_t(0, strlen(buf));
        }
    }
}

void test_proppatch_stops_when_full()
{
    const char *body = "<D:propertyupdate xmlns:D=\"DAV:\"><D:set><D:prop><D:displayname/></D:prop></D:set>"
                       "</D:propertyupdate>";
    char buf[40]; // too small for the whole document
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/file.txt", body, strlen(body));
    TEST_ASSERT_EQUAL_size_t(0, len);            // signals "did not fit"
    TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf)); // no overrun
}

void test_method_all_including_head()
{
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_HEAD, pc_webdav_method("HEAD"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_OPTIONS, pc_webdav_method("OPTIONS"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_MKCOL, pc_webdav_method("MKCOL"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_COPY, pc_webdav_method("COPY"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_MOVE, pc_webdav_method("MOVE"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_LOCK, pc_webdav_method("LOCK"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_UNLOCK, pc_webdav_method("UNLOCK"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_UNSUPPORTED, pc_webdav_method("BOGUS"));
    TEST_ASSERT_EQUAL_INT(WebDavMethod::DAV_M_UNSUPPORTED, pc_webdav_method(nullptr));
}

void test_depth_and_dest_path_guards()
{
    TEST_ASSERT_EQUAL_INT(7, pc_webdav_depth(nullptr, 7)); // null -> default
    TEST_ASSERT_EQUAL_INT(7, pc_webdav_depth("", 7));      // empty -> default
    TEST_ASSERT_EQUAL_INT(0, pc_webdav_depth("0", 7));
    TEST_ASSERT_EQUAL_INT(1, pc_webdav_depth("1", 7));
    char out[64];
    // Malformed %-escape in the destination path fails closed.
    TEST_ASSERT_FALSE(pc_webdav_dest_path("http://host/a%zzb", out, sizeof(out)));
    // A path that overflows the output buffer fails closed.
    TEST_ASSERT_FALSE(pc_webdav_dest_path("http://host/aaaaaaaaaaaaaaaaaaaaaaaaaaaa", out, 8));
}

void test_ms_entry_content_type_overflow()
{
    char buf[1024];
    size_t len = 0;
    char ct[420];
    for (int i = 0; i < 419; i++)
    {
        ct[i] = 'x';
    }
    ct[419] = '\0';
    // An oversized content-type overflows the internal element buffer -> len unchanged (atomic no-op).
    size_t r = pc_webdav_ms_entry(buf, sizeof(buf), len, "/f.txt", false, 100, "Mon, 01 Jan 2026 00:00:00 GMT", ct);
    TEST_ASSERT_EQUAL_size_t(len, r);
}

void test_ms_entry_mtime_and_tiny_buf()
{
    char buf[1024];
    char mtime[460];
    for (int i = 0; i < 459; i++)
    {
        mtime[i] = 'm';
    }
    mtime[459] = '\0';
    // Oversized mtime overflows the element buffer -> len unchanged.
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_ms_entry(buf, sizeof(buf), 0, "/f.txt", false, 100, mtime, "text/plain"));
    // A well-formed entry that does not fit the OUTPUT buffer leaves len unchanged (atomic commit fails).
    char tiny[40];
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_ms_entry(tiny, sizeof(tiny), 0, "/f.txt", false, 100, "", "text/plain"));
}

void test_proppatch_ms_echo()
{
    char buf[512];
    // A self-closed property with trailing whitespace exercises the open-tag trim.
    const char *body = "<D:propertyupdate><D:set><D:prop><D:author  /></D:prop></D:set></D:propertyupdate>";
    size_t n = pc_webdav_proppatch_ms(buf, sizeof(buf), "/file.txt", body, strlen(body));
    TEST_ASSERT_TRUE(n > 0);
    // A tiny buffer makes the leading append fail -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf, 20, "/file.txt", body, strlen(body)));
}

// ---------------------------------------------------------------------------
// Additional branch coverage: dest_path hex-digit combinations, ms_entry
// element-buffer edges, and proppatch scaffold / scan-loop edges.
// ---------------------------------------------------------------------------

void test_dest_path_valid_first_hex_invalid_second()
{
    // First hex digit valid, second invalid: distinct from an invalid FIRST digit
    // (the "%zz" cases above), which short-circuits before the second is checked.
    char out[64];
    TEST_ASSERT_FALSE(pc_webdav_dest_path("/dav/%2gfile", out, sizeof(out)));
}

void test_ms_entry_content_type_null_and_empty()
{
    char buf[1024];
    // content_type == nullptr: the getcontenttype block is skipped entirely.
    size_t r1 = pc_webdav_ms_entry(buf, sizeof(buf), 0, "/f.txt", false, 100, nullptr, nullptr);
    TEST_ASSERT_TRUE(r1 > 0);
    TEST_ASSERT_FALSE(contains(buf, "getcontenttype"));

    // content_type == "" (non-null, empty): same effect as nullptr.
    size_t r2 = pc_webdav_ms_entry(buf, sizeof(buf), 0, "/f.txt", false, 100, nullptr, "");
    TEST_ASSERT_TRUE(r2 > 0);
    TEST_ASSERT_FALSE(contains(buf, "getcontenttype"));
}

void test_ms_entry_getcontenttype_close_overflow()
{
    char buf[1024];
    char ct[301];
    for (int i = 0; i < 300; i++)
    {
        ct[i] = 'c';
    }
    ct[300] = '\0';
    // The content_type itself fits the internal 512-byte element scratch, but the
    // closing "</D:getcontenttype>" tag appended afterward then overflows it ->
    // len unchanged. Distinct from the content_type-itself-overflowing case above.
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_ms_entry(buf, sizeof(buf), 0, "/f.txt", false, 100, nullptr, ct));
}

void test_ms_entry_mtime_prefix_and_close_overflow()
{
    char buf[1024];
    // A large-but-fitting content_type leaves just enough of the internal 512-byte
    // scratch used up that the (fixed, tiny) getlastmodified *prefix* itself overflows.
    char ct[291];
    for (int i = 0; i < 290; i++)
    {
        ct[i] = 'c';
    }
    ct[290] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_ms_entry(buf, sizeof(buf), 0, "/f.txt", false, 100, "x", ct));

    // No content_type this time: the mtime prefix and the mtime content both fit,
    // but the closing "</D:getlastmodified>" tag then overflows.
    char mtime[301];
    for (int i = 0; i < 300; i++)
    {
        mtime[i] = 'm';
    }
    mtime[300] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_ms_entry(buf, sizeof(buf), 0, "/f.txt", false, 100, mtime, nullptr));
}

void test_proppatch_zero_cap()
{
    char buf[1] = {'z'};
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf, 0, "/x", "", 0));
}

void test_proppatch_scaffold_esc_and_closer_overflow()
{
    // Preamble fits but the escaped href itself overflows the output buffer.
    char href[151];
    for (int i = 0; i < 150; i++)
    {
        href[i] = 'h';
    }
    href[150] = '\0';
    char buf1[120];
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf1, sizeof(buf1), href, "", 0));

    // Preamble and href fit, but the "</D:href>...<D:prop>" scaffold closer overflows.
    char buf2[135];
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf2, sizeof(buf2), "/x", "", 0));
}

void test_proppatch_emitted_cap_stops_scan()
{
    // 20 self-closed properties, more than PC_WEBDAV_MAX_PROPS (16): the scanner
    // stops emitting once the cap is reached even though the body has more left.
    const char *body = "<D:propertyupdate><D:set><D:prop>"
                       "<a/><b/><c/><d/><e/><f/><g/><h/><i/><j/><k/><l/><m/><n/><o/><p/><q/><r/><s/><t/>"
                       "</D:prop></D:set></D:propertyupdate>";
    char buf[2048];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<p/>"));  // the 16th property (a..p) is emitted
    TEST_ASSERT_FALSE(contains(buf, "<q/>")); // the 17th onward is not: cap reached
}

void test_proppatch_tag_name_whitespace_terminators()
{
    // A tab, CR, and LF each directly terminate a property's local name (in addition
    // to the already-covered space and '/').
    const char *body = "<D:propertyupdate><D:set><D:prop><a\tb/><c\rd/><e\nf/></D:prop></D:set></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
}

void test_proppatch_self_closed_prop_wrapper()
{
    // <D:prop/> as a fully self-closed, empty property set: is_prop is true but
    // in_prop is never set (there is nothing inside to refuse).
    const char *body = "<D:propertyupdate><D:set><D:prop/></D:set></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:prop>"));
}

void test_proppatch_trailing_whitespace_trim()
{
    // A property whose local name is followed by tab, CR, and LF (trimmed off,
    // right-to-left) before the self-closing "/>".
    const char *body = "<D:propertyupdate><D:set><D:prop><D:trailer\t\r\n/></D:prop></D:set></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:trailer/>"));
}

void test_proppatch_empty_after_trim()
{
    // A property whose entire span is whitespace (trimming walks all the way back
    // to `start`): rejected outright (nothing echoed), not just truncated.
    const char *body = "<D:propertyupdate><D:set><D:prop>< /></D:prop></D:set></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
}

void test_proppatch_oversized_tag_name_skipped()
{
    // A property whose (trimmed) tag content is >= the internal 256-byte tag[]
    // scratch: silently skipped (not echoed), rather than truncated or overflowed.
    char body[512];
    const char *prefix = "<D:propertyupdate><D:set><D:prop><";
    const char *suffix = "/></D:prop></D:set></D:propertyupdate>";
    size_t p = strlen(prefix);
    memcpy(body, prefix, p);
    for (int i = 0; i < 300; i++)
    {
        body[p + i] = 'x';
    }
    strcpy(body + p + 300, suffix);
    char buf[2048];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_FALSE(contains(buf, "xxxxxxxxxx"));
}

void test_proppatch_echo_append_boundary_failures()
{
    const char *body = "<D:propertyupdate><D:set><D:prop><a/></D:prop></D:set></D:propertyupdate>";
    // The scaffold ("<?xml...<D:href>/x</D:href>...<D:prop>") fits exactly at 141
    // bytes; each cap below stops the property echo at a different one of its
    // three atomic appends ("        <", the tag itself, "/>\n").
    char buf[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf, 145, "/x", body, strlen(body))); // "        <" fails
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf, 151, "/x", body, strlen(body))); // tag content fails
    TEST_ASSERT_EQUAL_size_t(0, pc_webdav_proppatch_ms(buf, 153, "/x", body, strlen(body))); // "/>\n" fails
}

void test_proppatch_embedded_lt_in_value()
{
    // A property value containing a '<' that is NOT the start of its closing tag
    // ("</..."); the value-skip loop must keep scanning past it instead of stopping.
    const char *body = "<D:propertyupdate><D:set><D:prop>"
                       "<D:getlastmodified>a<bc</D:getlastmodified>"
                       "</D:prop></D:set></D:propertyupdate>";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:getlastmodified/>"));
}

void test_proppatch_truncated_closing_tag()
{
    // The property's value is never given a proper closing tag (no '>' appears
    // anywhere after the "</" that starts it): the skip-to-'>' loop runs off the
    // end of body_len instead of finding one, the outer scan then ends cleanly
    // (i reaches body_len), and the 403 envelope is still completed normally.
    const char *body = "<D:propertyupdate><D:set><D:prop><D:foo>value</D:fo";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:foo/>"));
}

void test_proppatch_value_scan_runs_to_body_end()
{
    // The property's value contains no "</" anywhere before body_len: the
    // value-skip loop's own bounds check (j + 1 < body_len), not a "</" match,
    // is what ends the scan. Distinct from test_proppatch_truncated_closing_tag
    // above, whose value DOES contain a "</" (just no terminating '>').
    const char *body = "<D:propertyupdate><D:set><D:prop><D:foo>plainvalue-with-no-closing-marker-at-all";
    char buf[512];
    size_t len = pc_webdav_proppatch_ms(buf, sizeof(buf), "/dav/f", body, strlen(body));
    assert_proppatch_envelope(buf, len);
    TEST_ASSERT_TRUE(contains(buf, "<D:foo/>"));
}

// ── lock manager (RFC 4918 §6-7) ─────────────────────────────────────────────────────────────────
void test_lock_acquire_and_write_gate()
{
    DavLockTable t;
    pc_dav_lock_init(&t);
    const char *tok = "opaquelocktoken:11111111-pc";

    // An unlocked resource is writable by anyone.
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/a.txt", nullptr));

    // Take an exclusive Depth-0 lock; now only the token holder may write.
    const DavLock *l = pc_dav_lock_acquire(&t, "/a.txt", tok, /*exclusive=*/true, /*depth_infinity=*/false, 0);
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/a.txt", nullptr));                 // no token
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/a.txt", "opaquelocktoken:other")); // wrong token
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/a.txt", tok));                      // right token
    // A sibling is unaffected by a Depth-0 lock.
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/b.txt", nullptr));

    // UNLOCK by token frees it.
    TEST_ASSERT_TRUE(pc_dav_lock_release(&t, tok));
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/a.txt", nullptr));
    TEST_ASSERT_FALSE(pc_dav_lock_release(&t, tok)); // already gone
}

void test_lock_depth_infinity_covers_subtree()
{
    DavLockTable t;
    pc_dav_lock_init(&t);
    const char *tok = "opaquelocktoken:22222222-pc";
    // A Depth-infinity lock on /dir covers the whole subtree, but not a same-prefix sibling like /dir2.
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, "/dir", tok, true, /*depth_infinity=*/true, 0));
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/dir", nullptr));
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/dir/sub/file.txt", nullptr)); // deep descendant locked
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/dir/sub/file.txt", tok));      // token unlocks the subtree
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/dir2/file.txt", nullptr));     // "/dir2" is NOT under "/dir"

    // pc_dav_lock_find reports the covering lock for a descendant, and a trailing slash is normalized.
    TEST_ASSERT_NOT_NULL(pc_dav_lock_find(&t, "/dir/sub"));
    TEST_ASSERT_NOT_NULL(pc_dav_lock_find(&t, "/dir/"));
    TEST_ASSERT_NULL(pc_dav_lock_find(&t, "/dir2"));
}

void test_lock_conflicts_and_shared()
{
    DavLockTable t;
    pc_dav_lock_init(&t);
    // An exclusive infinity lock on /p blocks any overlapping lock (child, ancestor, or same).
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, "/p", "opaquelocktoken:a", true, true, 0));
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&t, "/p/c", "opaquelocktoken:b", true, false, 0)); // child conflicts
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&t, "/p", "opaquelocktoken:b", false, false, 0));  // same resource
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&t, "/", "opaquelocktoken:b", true, true, 0));     // ancestor (root) conflicts
    // A disjoint path is fine.
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, "/q", "opaquelocktoken:c", true, false, 0));

    // Two shared locks on the same resource coexist; an exclusive one over them does not.
    DavLockTable s;
    pc_dav_lock_init(&s);
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&s, "/f", "opaquelocktoken:s1", false, false, 0));
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&s, "/f", "opaquelocktoken:s2", false, false, 0));
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&s, "/f", "opaquelocktoken:x", true, false, 0));
    // Either shared token authorizes a write.
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&s, "/f", "opaquelocktoken:s2"));
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&s, "/f", nullptr));
}

void test_lock_table_full_and_guards()
{
    DavLockTable t;
    pc_dav_lock_init(&t);
    char path[16], tok[40];
    for (int i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        snprintf(path, sizeof(path), "/f%d", i);
        snprintf(tok, sizeof(tok), "opaquelocktoken:%d", i);
        TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, path, tok, true, false, 0));
    }
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&t, "/overflow", "opaquelocktoken:z", true, false, 0)); // table full

    // A token that would not fit is rejected; null arguments fail closed.
    DavLockTable t2;
    pc_dav_lock_init(&t2);
    char longtok[PC_DAV_LOCK_TOKEN_MAX + 8];
    memset(longtok, 'x', sizeof(longtok) - 1);
    longtok[sizeof(longtok) - 1] = 0;
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&t2, "/a", longtok, true, false, 0));
    TEST_ASSERT_NULL(pc_dav_lock_acquire(&t2, nullptr, "opaquelocktoken:a", true, false, 0));
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(nullptr, "/a", nullptr)); // no table => unlocked
}

void test_if_header_token_extraction()
{
    char out[48];
    // Untagged condition list.
    TEST_ASSERT_TRUE(pc_dav_if_token("(<opaquelocktoken:aaaa-pc>)", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("opaquelocktoken:aaaa-pc", out);
    // Tagged list: the resource URL before the '(' must be skipped.
    TEST_ASSERT_TRUE(pc_dav_if_token("</dir/file.txt> (<opaquelocktoken:bbbb-pc>)", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("opaquelocktoken:bbbb-pc", out);
    // A "Not" condition still yields the first token.
    TEST_ASSERT_TRUE(pc_dav_if_token("(Not <opaquelocktoken:cccc>)", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("opaquelocktoken:cccc", out);
    // No parenthesized list, an unterminated token, and a null all fail.
    TEST_ASSERT_FALSE(pc_dav_if_token("no-list-here", out, sizeof(out)));
    TEST_ASSERT_FALSE(pc_dav_if_token("(<unterminated", out, sizeof(out)));
    TEST_ASSERT_FALSE(pc_dav_if_token(nullptr, out, sizeof(out)));

    // End-to-end: a write presenting the extracted token is allowed.
    DavLockTable t;
    pc_dav_lock_init(&t);
    pc_dav_lock_acquire(&t, "/x", "opaquelocktoken:aaaa-pc", true, false, 0);
    TEST_ASSERT_TRUE(pc_dav_if_token("(<opaquelocktoken:aaaa-pc>)", out, sizeof(out)));
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/x", out));
}

void test_lock_timeout_sweep()
{
    DavLockTable t;
    pc_dav_lock_init(&t);

    // A timed lock gates writes until a sweep reaches its expiry second (expiry_s <= now drops it).
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, "/timed", "opaquelocktoken:t", true, false, /*expiry_s=*/100));
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/timed", nullptr));
    TEST_ASSERT_EQUAL_size_t(0, pc_dav_lock_sweep(&t, 99)); // not yet expired
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/timed", nullptr));
    TEST_ASSERT_EQUAL_size_t(1, pc_dav_lock_sweep(&t, 100)); // expired -> dropped
    TEST_ASSERT_TRUE(pc_dav_lock_can_write(&t, "/timed", nullptr));
    TEST_ASSERT_NULL(pc_dav_lock_find(&t, "/timed"));

    // A never-expiring lock (expiry_s == 0) survives any sweep.
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, "/perm", "opaquelocktoken:p", true, false, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_dav_lock_sweep(&t, 0xFFFFFFFFu));
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/perm", nullptr));

    // Refresh extends a lock's timeout; an unknown token refreshes nothing.
    TEST_ASSERT_NOT_NULL(pc_dav_lock_acquire(&t, "/r", "opaquelocktoken:r", true, false, 50));
    TEST_ASSERT_NOT_NULL(pc_dav_lock_refresh(&t, "opaquelocktoken:r", 200));
    TEST_ASSERT_EQUAL_size_t(0, pc_dav_lock_sweep(&t, 100)); // was 50, now 200 -> survives
    TEST_ASSERT_FALSE(pc_dav_lock_can_write(&t, "/r", nullptr));
    TEST_ASSERT_NULL(pc_dav_lock_refresh(&t, "opaquelocktoken:unknown", 300));
    TEST_ASSERT_EQUAL_size_t(1, pc_dav_lock_sweep(&t, 200)); // /r now expires (/perm never does)
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_method_classification);
    RUN_TEST(test_webdav_builder_guards);
    RUN_TEST(test_depth_parsing);
    RUN_TEST(test_xml_escape);
    RUN_TEST(test_xml_escape_truncates_safely);
    RUN_TEST(test_dest_absolute_uri);
    RUN_TEST(test_dest_percent_decoded);
    RUN_TEST(test_dest_abs_path);
    RUN_TEST(test_dest_rejects_malformed);
    RUN_TEST(test_multistatus_file_and_collection);
    RUN_TEST(test_multistatus_escapes_href);
    RUN_TEST(test_multistatus_entry_stops_when_full);
    RUN_TEST(test_proppatch_windows_timestamp);
    RUN_TEST(test_proppatch_multiple_and_self_closed);
    RUN_TEST(test_proppatch_remove_block);
    RUN_TEST(test_proppatch_escapes_href);
    RUN_TEST(test_proppatch_empty_body_is_valid);
    RUN_TEST(test_proppatch_rejects_injection);
    RUN_TEST(test_proppatch_fuzz_bounded);
    RUN_TEST(test_proppatch_stops_when_full);
    RUN_TEST(test_method_all_including_head);
    RUN_TEST(test_depth_and_dest_path_guards);
    RUN_TEST(test_ms_entry_content_type_overflow);
    RUN_TEST(test_ms_entry_mtime_and_tiny_buf);
    RUN_TEST(test_proppatch_ms_echo);
    RUN_TEST(test_dest_path_valid_first_hex_invalid_second);
    RUN_TEST(test_ms_entry_content_type_null_and_empty);
    RUN_TEST(test_ms_entry_getcontenttype_close_overflow);
    RUN_TEST(test_ms_entry_mtime_prefix_and_close_overflow);
    RUN_TEST(test_proppatch_zero_cap);
    RUN_TEST(test_proppatch_scaffold_esc_and_closer_overflow);
    RUN_TEST(test_proppatch_emitted_cap_stops_scan);
    RUN_TEST(test_proppatch_tag_name_whitespace_terminators);
    RUN_TEST(test_proppatch_self_closed_prop_wrapper);
    RUN_TEST(test_proppatch_trailing_whitespace_trim);
    RUN_TEST(test_proppatch_empty_after_trim);
    RUN_TEST(test_proppatch_oversized_tag_name_skipped);
    RUN_TEST(test_proppatch_echo_append_boundary_failures);
    RUN_TEST(test_proppatch_embedded_lt_in_value);
    RUN_TEST(test_proppatch_truncated_closing_tag);
    RUN_TEST(test_proppatch_value_scan_runs_to_body_end);
    RUN_TEST(test_lock_acquire_and_write_gate);
    RUN_TEST(test_lock_depth_infinity_covers_subtree);
    RUN_TEST(test_lock_conflicts_and_shared);
    RUN_TEST(test_lock_table_full_and_guards);
    RUN_TEST(test_if_header_token_extraction);
    RUN_TEST(test_lock_timeout_sweep);
    return UNITY_END();
}
