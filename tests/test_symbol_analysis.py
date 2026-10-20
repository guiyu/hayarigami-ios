"""Synthetic ELF symbol bindings and bounds, independent of game files."""
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze import nso_info
from audit_reference import import_summary


def module():
    image=bytearray(0x300)
    struct.pack_into('<I',image,4,0x20);image[0x20:0x24]=b'MOD0';struct.pack_into('<i',image,0x24,0x60)
    for i,pair in enumerate([(4,0x200),(6,0x100),(5,0x180),(10,32),(11,24),(0,0)]):
        struct.pack_into('<QQ',image,0x80+i*16,*pair)
    struct.pack_into('<II',image,0x200,1,3)
    names=b'\0optional\0required\0';image[0x180:0x180+len(names)]=names
    struct.pack_into('<IBBHQQ',image,0x118,1,0x22,2,0,0,0)
    struct.pack_into('<IBBHQQ',image,0x130,10,0x12,0,0,0,0)
    header=bytearray(256);header[:4]=b'NSO0'
    for i,(fo,mo,size) in enumerate([(256,0,len(image)),(256+len(image),len(image),0),(256+len(image),len(image),0)]):
        struct.pack_into('<III',header,16+16*i,fo,mo,size)
    return header+image


class SymbolAnalysisTests(unittest.TestCase):
    def test_binding_and_visibility_survive_parsing(self):
        report,_=nso_info(module());weak,strong=report['symbols']
        self.assertEqual((weak['binding'],weak['visibility'],weak['section']),(2,2,0))
        self.assertEqual(strong['binding'],1)
        summary=import_summary({'modules':{'main':report}})
        self.assertEqual(summary['unresolved_weak_imports'],['optional'])
        self.assertEqual(summary['unresolved_strong_imports'],['required'])
        self.assertEqual(summary['unresolved_unknown_binding_imports'],[])

    def test_defined_symbol_resolves_weak_reference_and_legacy_is_unknown(self):
        report={'modules':{'main':{'symbols':[dict(name='found',defined=False,binding=2),dict(name='legacy',defined=False)]},
                            'sdk':{'symbols':[dict(name='found',defined=True,binding=1)]}}}
        result=import_summary(report)
        self.assertEqual(result['imports_without_module_definition'],['legacy'])
        self.assertEqual(result['unresolved_unknown_binding_imports'],['legacy'])
        self.assertEqual(result['unresolved_weak_imports'],[])

    def test_negative_dynamic_offset_is_rejected(self):
        data=module();struct.pack_into('<i',data,256+0x24,-0x30)
        with self.assertRaisesRegex(ValueError,'dynamic.*bounds'):
            nso_info(data)

    def test_symbol_table_cannot_escape_image(self):
        data=module();struct.pack_into('<Q',data,256+0x80+16+8,0x2f0)
        with self.assertRaisesRegex(ValueError,'symbol.*bounds'):
            nso_info(data)

    def test_string_table_cannot_be_silently_truncated(self):
        data=module();struct.pack_into('<Q',data,256+0x80+2*16+8,0x2f0)
        with self.assertRaisesRegex(ValueError,'string.*bounds'):
            nso_info(data)


if __name__=='__main__':unittest.main()
