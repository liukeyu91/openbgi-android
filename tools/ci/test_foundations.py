import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import warnings
import zipfile

ROOT=Path(__file__).resolve().parents[2]
def module(name, file):
    spec=importlib.util.spec_from_file_location(name, file)
    result=importlib.util.module_from_spec(spec); spec.loader.exec_module(result); return result
signing=module('signing', ROOT/'tools/android/signing.py')
zipper=module('zipper', ROOT/'tools/android/normalize-zip.py')

class Foundations(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name)
    def test_zip_is_independent_of_order_timestamp_permissions_and_compression(self):
        for n, names in enumerate((['a','b'],['b','a'])):
            with zipfile.ZipFile(self.base/f'{n}.zip','w') as z:
                for name in names:
                    info=zipfile.ZipInfo(name,(2020+n,1,1,0,0,0)); info.external_attr=(0o600+n)<<16
                    info.compress_type=zipfile.ZIP_STORED if n else zipfile.ZIP_DEFLATED
                    z.writestr(info,name.encode()*100)
            zipper.normalize(self.base/f'{n}.zip',self.base/f'{n}.out',1700000000)
        self.assertEqual((self.base/'0.out').read_bytes(),(self.base/'1.out').read_bytes())
    def test_zip_rejects_duplicates_and_traversal(self):
        for names in (['a','a'],['../outside'],['/absolute']):
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')
                with zipfile.ZipFile(self.base/'bad.zip','w') as z:
                    for name in names: z.writestr(name,b'x')
            with self.assertRaises(ValueError): zipper.normalize(self.base/'bad.zip',self.base/'out.zip',1700000000)
    def test_clean_is_atomic_when_a_key_is_found_and_refuses_outside_paths(self):
        root=self.base/'repo'; (root/'tools').mkdir(parents=True)
        shutil.copy2(ROOT/'tools/clean.py',root/'tools/clean.py')
        (root/'obj').mkdir(); (root/'obj/sentinel').write_text('keep')
        (root/'bin').mkdir(); (root/'bin/release.p12').write_bytes(b'key')
        def clean(*args): return subprocess.run(['python3',str(root/'tools/clean.py'),*args],capture_output=True)
        self.assertNotEqual(clean('obj','bin').returncode,0)
        self.assertTrue((root/'obj/sentinel').exists())
        self.assertNotEqual(clean('../outside').returncode,0)
        (root/'bin/release.p12').unlink()
        self.assertEqual(clean('obj','bin').returncode,0)
        self.assertFalse((root/'obj').exists())
    def test_missing_or_in_tree_key_never_generates_a_replacement(self):
        missing=self.base/'missing.p12'
        for key in (missing, ROOT/'bin/missing.p12'):
            with self.assertRaises(ValueError): signing.preflight(ROOT,ROOT/'tools/android/AndroidManifest.xml',key,'test')
        self.assertFalse(missing.exists())
    def test_real_certificate_pin_and_wrong_identity(self):
        root=self.base/'repo'; (root/'tools/android').mkdir(parents=True)
        manifest=root/'AndroidManifest.xml'; manifest.write_text('<manifest package="test.identity"/>')
        pins=root/'tools/android/release-certificates.json'
        key=self.base/'test.p12'
        env=dict(os.environ,OPENBGI_STORE_PASS='temporary-test-password')
        subprocess.run(['keytool','-genkeypair','-noprompt','-alias','test','-keyalg','RSA','-keysize','2048','-validity','2','-dname','CN=Build Test','-storetype','PKCS12','-keystore',str(key),'-storepass:env','OPENBGI_STORE_PASS'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
        import hashlib
        der=subprocess.check_output(['keytool','-exportcert','-alias','test','-keystore',str(key),'-storepass:env','OPENBGI_STORE_PASS'],env=env)
        expected=hashlib.sha256(der).hexdigest(); pins.write_text(json.dumps({'test.identity':expected}))
        from unittest.mock import patch
        with patch.dict(os.environ,env,clear=True):
            os.environ.pop('OPENBGI_EXPECTED_CERT_SHA256',None)
            self.assertEqual(signing.preflight(root,manifest,key,'test'),expected)
            pins.write_text(json.dumps({'test.identity':'0'*64}))
            with self.assertRaisesRegex(ValueError,'differs'): signing.preflight(root,manifest,key,'test')

if __name__=='__main__': unittest.main()
