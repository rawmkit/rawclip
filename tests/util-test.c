#include "util.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
    static const unsigned char good[] = "hello \xe2\x82\xac";
    static const unsigned char bad_overlong[] = {0xc0, 0x80};
    static const unsigned char bad_split[] = {0xe2, 0x82};
    static const unsigned char latin1[] = {'A', 0xe9};
    char *utf8 = NULL;
    size_t utf8_len = 0U;
    unsigned char *roundtrip = NULL;
    size_t roundtrip_len = 0U;

    assert(rawclip_utf8_valid(good, sizeof(good) - 1U));
    assert(!rawclip_utf8_valid(bad_overlong, sizeof(bad_overlong)));
    assert(!rawclip_utf8_valid(bad_split, sizeof(bad_split)));
    assert(rawclip_latin1_to_utf8(latin1, sizeof(latin1), &utf8, &utf8_len) == 0);
    assert(rawclip_utf8_valid((const unsigned char *)utf8, utf8_len));
    assert(rawclip_utf8_to_latin1(utf8, utf8_len, &roundtrip, &roundtrip_len) == 0);
    assert(roundtrip_len == sizeof(latin1));
    assert(memcmp(roundtrip, latin1, sizeof(latin1)) == 0);
    free(utf8);
    free(roundtrip);

    errno = 0;
    assert(rawclip_utf8_to_latin1("\xe2\x82\xac", 3U, &roundtrip, &roundtrip_len) == -1);
    assert(errno == EILSEQ);
    {
        uint64_t id = 0U;
        assert(rawclip_parse_u64("1", &id) == 0 && id == 1U);
        assert(rawclip_parse_u64("00042", &id) == 0 && id == 42U);
        assert(rawclip_parse_u64("0", &id) == -1);
        assert(rawclip_parse_u64("+1", &id) == -1);
        assert(rawclip_parse_u64(" 1", &id) == -1);
        assert(rawclip_parse_u64("1 ", &id) == -1);
        assert(rawclip_parse_u64("18446744073709551616", &id) == -1);
    }

    return 0;
}
