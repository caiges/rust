use std;
import std::str;

#[test]
fn test_bytes_len() {
    assert (str::byte_len("") == 0u);
    assert (str::byte_len("hello world") == 11u);
    assert (str::byte_len("\x63") == 1u);
    assert (str::byte_len("\xa2") == 2u);
    assert (str::byte_len("\u03c0") == 2u);
    assert (str::byte_len("\u2620") == 3u);
    assert (str::byte_len("\U0001d11e") == 4u);
}

#[test]
fn test_index_and_rindex() {
    assert (str::index("hello", 'e' as u8) == 1);
    assert (str::index("hello", 'o' as u8) == 4);
    assert (str::index("hello", 'z' as u8) == -1);
    assert (str::rindex("hello", 'l' as u8) == 3);
    assert (str::rindex("hello", 'h' as u8) == 0);
    assert (str::rindex("hello", 'z' as u8) == -1);
}

#[test]
fn test_split() {
    fn t(s: &str, c: char, i: int, k: &str) {
        log "splitting: " + s;
        log i;
        let v = str::split(s, c as u8);
        log "split to: ";
        for z: str  in v { log z; }
        log "comparing: " + v.(i) + " vs. " + k;
        assert (str::eq(v.(i), k));
    }
    t("abc.hello.there", '.', 0, "abc");
    t("abc.hello.there", '.', 1, "hello");
    t("abc.hello.there", '.', 2, "there");
    t(".hello.there", '.', 0, "");
    t(".hello.there", '.', 1, "hello");
    t("...hello.there.", '.', 3, "hello");
    t("...hello.there.", '.', 5, "");
}

#[test]
fn test_find() {
    fn t(haystack: &str, needle: &str, i: int) {
        let j: int = str::find(haystack, needle);
        log "searched for " + needle;
        log j;
        assert (i == j);
    }
    t("this is a simple", "is a", 5);
    t("this is a simple", "is z", -1);
    t("this is a simple", "", 0);
    t("this is a simple", "simple", 10);
    t("this", "simple", -1);
}

#[test]
fn test_substr() {
    fn t(a: &str, b: &str, start: int) {
        assert (str::eq(str::substr(a, start as uint, str::byte_len(b)), b));
    }
    t("hello", "llo", 2);
    t("hello", "el", 1);
    t("substr should not be a challenge", "not", 14);
}

#[test]
fn test_concat() {
    fn t(v: &vec[str], s: &str) { assert (str::eq(str::concat(v), s)); }
    t(["you", "know", "I'm", "no", "good"], "youknowI'mnogood");
    let v: vec[str] = [];
    t(v, "");
    t(["hi"], "hi");
}

#[test]
fn test_connect() {
    fn t(v: &vec[str], sep: &str, s: &str) {
        assert (str::eq(str::connect(v, sep), s));
    }
    t(["you", "know", "I'm", "no", "good"], " ", "you know I'm no good");
    let v: vec[str] = [];
    t(v, " ", "");
    t(["hi"], " ", "hi");
}

#[test]
fn test_to_upper() {
    // to_upper doesn't understand unicode yet,
    // but we need to at least preserve it

    let unicode = "\u65e5\u672c";
    let input = "abcDEF" + unicode + "xyz:.;";
    let expected = "ABCDEF" + unicode + "XYZ:.;";
    let actual = str::to_upper(input);
    assert (str::eq(expected, actual));
}

#[test]
fn test_slice() {
    assert (str::eq("ab", str::slice("abc", 0u, 2u)));
    assert (str::eq("bc", str::slice("abc", 1u, 3u)));
    assert (str::eq("", str::slice("abc", 1u, 1u)));
    fn a_million_letter_a() -> str {
        let i = 0;
        let rs = "";
        while i < 100000 { rs += "aaaaaaaaaa"; i += 1; }
        ret rs;
    }
    fn half_a_million_letter_a() -> str {
        let i = 0;
        let rs = "";
        while i < 100000 { rs += "aaaaa"; i += 1; }
        ret rs;
    }
    assert (str::eq(half_a_million_letter_a(),
                    str::slice(a_million_letter_a(), 0u, 500000u)));
}

#[test]
fn test_ends_with() {
    assert (str::ends_with("", ""));
    assert (str::ends_with("abc", ""));
    assert (str::ends_with("abc", "c"));
    assert (!str::ends_with("a", "abc"));
    assert (!str::ends_with("", "abc"));
}

#[test]
fn test_is_empty() {
    assert (str::is_empty(""));
    assert (!str::is_empty("a"));
}

#[test]
fn test_is_not_empty() {
    assert (str::is_not_empty("a"));
    assert (!str::is_not_empty(""));
}

#[test]
fn test_replace() {
    let a = "a";
    check (str::is_not_empty(a));
    assert (str::replace("", a, "b") == "");
    assert (str::replace("a", a, "b") == "b");
    assert (str::replace("ab", a, "b") == "bb");
    let test = "test";
    check (str::is_not_empty(test));
    assert (str::replace(" test test ", test, "toast") == " toast toast ");
    assert (str::replace(" test test ", test, "") == "   ");
}


// Local Variables:
// mode: rust;
// fill-column: 78;
// indent-tabs-mode: nil
// c-basic-offset: 4
// buffer-file-coding-system: utf-8-unix
// compile-command: "make -k -C $RBUILD 2>&1 | sed -e 's/\\/x\\//x:\\//g'";
// End: