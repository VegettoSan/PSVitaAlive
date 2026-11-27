import io
import struct
import unittest
import zipfile

from scripts.vpk_display_name import sfo_display_name, vpk_display_name


def sfo(entries):
    keys, values, table = bytearray(), bytearray(), bytearray()
    for key, value in entries:
        data = value if isinstance(value, bytes) else value.encode("utf-8") + b"\0"
        table.extend(struct.pack("<HHIII", len(keys), 0x0204, len(data), len(data), len(values)))
        keys.extend(key.encode("ascii")+b"\0")
        values.extend(data)
    return struct.pack("<4sIIII", b"\0PSF", 0x101, 20+len(table),
                       20+len(table)+len(keys), len(entries))+table+keys+values


class DisplayNameTests(unittest.TestCase):
    def test_full_title_wins_over_short_and_localized(self):
        self.assertEqual(sfo_display_name(sfo([("STITLE","Short"),("TITLE_01","English"),
                                               ("TITLE","Aventuras de José") ])), ("Aventuras de José","TITLE"))

    def test_localized_then_short_fallback(self):
        self.assertEqual(sfo_display_name(sfo([("TITLE"," "),("TITLE_01","日本語"),("STITLE","Short")])),
                         ("日本語","TITLE_01"))
        self.assertEqual(sfo_display_name(sfo([("STITLE","Short")])), ("Short","STITLE"))

    def test_bad_values_and_duplicates_do_not_override(self):
        for value in ("Unknown","APP","TEST00001",b"\xff\0",b"Hello\0world\0","A"*129,"bad\u202ename"):
            self.assertEqual(sfo_display_name(sfo([("TITLE",value),("STITLE","Real Name")])), ("Real Name","STITLE"))
        self.assertEqual(sfo_display_name(sfo([("TITLE","One"),("TITLE","Two"),("STITLE","Real Name")])),
                         ("Real Name","STITLE"))

    def test_no_title_or_malformed(self):
        for raw in (b"not sfo",sfo([("TITLE_ID","DEMO00001")]),sfo([("TITLE","Name")])[:25]):
            self.assertEqual(sfo_display_name(raw),("",""))

    def test_read_package_title(self):
        buf=io.BytesIO()
        with zipfile.ZipFile(buf,"w") as z:
            z.writestr("sce_sys/param.sfo",sfo([("TITLE_ID","DEMO00001"),("TITLE","Actual name")]))
        self.assertEqual(vpk_display_name(buf),("Actual name","TITLE"))


if __name__=="__main__":
    unittest.main()
