"""Real native faults in disposable processes: exercise the production reporter."""
import os
import hashlib
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest
import sys

ROOT=Path(__file__).resolve().parents[2]

@unittest.skipUnless(os.name=='posix','native signal tests require Linux')
class NativeCrash(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.base=Path(cls.temp.name);cls.binary=cls.base/'crash-fixture'
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c99','-D_GNU_SOURCE','-O1','-g',
            '-fno-omit-frame-pointer','-fno-optimize-sibling-calls','-I'+str(ROOT/'inc'),
            str(ROOT/'tools/tests/androidcrash.c'),str(ROOT/'src/os/android/crash.c'),
            '-pthread','-ldl','-Wl,--build-id=sha1','-o',str(cls.binary)],check=True)
    def run_case(self,mode,modules=()):
        directory=self.base/mode;directory.mkdir(exist_ok=True)
        metadata='Package: test.fixture\nEncoding: utf8\nEngine selection: 1.659.8\n'
        for label,elf in zip(('Native','Upstream','Bridge'),(self.binary,*modules)):
            metadata+=f'{label} SHA256: {hashlib.sha256(elf.read_bytes()).hexdigest()}\n'
        (directory/'session-current.txt').write_text(metadata)
        result=subprocess.run([str(self.binary),str(directory),mode,*map(str,modules)],timeout=8,capture_output=True)
        report=(directory/'native-crash.txt').read_text()
        self.assertEqual(result.returncode,134 if mode=='abort' else 139,result.stderr)
        self.assertIn('OpenBGI native crash report v2',report)
        self.assertIn('Encoding: utf8',report)
        self.assertRegex(report,r'pc: 0x[1-9a-f][0-9a-f]+')
        self.assertIn('cnfgwndtxt._bp',report) if mode!='concurrent' else None
        self.assertIn('End. Terminated',report)
        self.assertLess(len(report),300*1024)
        return directory,report
    def test_fault_address_frames_and_last_instructions(self):
        _,report=self.run_case('segv')
        self.assertIn('Fault address: 0x1\n',report)
        records=[line for line in report.splitlines() if line.startswith('T7 ')]
        self.assertEqual(len(records),32)
        self.assertIn('+0x30 ',records[0]);self.assertIn('+0x4f ',records[-1])
        # Resolve the *actual fault PC*, allowing ASLR, to its source line.
        pc=int(re.search(r'^pc: 0x([0-9a-f]+)',report,re.M)[1],16)
        bias=int(re.search(r'Engine ELF load bias: 0x([0-9a-f]+)',report)[1],16)
        result=subprocess.check_output(['addr2line','-f','-e',str(self.binary),hex(pc-bias)],text=True)
        self.assertIn('FaultLeaf',result);self.assertIn('androidcrash.c:',result)
        self.assertRegex(report,rf'(?m)^[0-9a-f]+-[0-9a-f]+ r-xp .*{re.escape(self.binary.name)}$')
    def test_abort_is_distinguished(self):
        _,report=self.run_case('abort');self.assertIn('Signal: 6\n',report)
    def test_symbolizer_verifies_identity_and_resolves_source(self):
        directory,_=self.run_case('symbols')
        debug=directory/'fixture.debug'
        subprocess.run(['objcopy','--only-keep-debug',str(self.binary),str(debug)],check=True)
        command=[sys.executable,str(ROOT/'tools/android/symbolize-crash.py'),str(directory/'native-crash.txt'),str(self.binary),str(debug),'--addr2line','addr2line']
        result=subprocess.run(command,text=True,capture_output=True,timeout=8)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('FaultLeaf',result.stdout);self.assertIn('androidcrash.c:',result.stdout)
        bad=directory/'other-elf';bad.write_bytes(self.binary.read_bytes()+b'different build')
        command[3]=str(bad)
        result=subprocess.run(command,text=True,capture_output=True,timeout=8)
        self.assertNotEqual(result.returncode,0);self.assertIn('SHA256',result.stderr)
    def test_comparison_report_keeps_module_identity(self):
        source=self.base/'compare-module.c'
        source.write_text('static volatile unsigned long badAddress=1;\n'
                          'void CrashFixtureFault(void){ *(volatile unsigned*)badAddress=7; }\n')
        modules=[self.base/'libfixture-upstream.so',self.base/'libfixture-bridge.so']
        for module in modules:
            subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-g','-shared','-fPIC',str(source),
                            f'-Wl,--build-id=sha1,-soname,{module.name}','-o',str(module)],check=True)
        directory,report=self.run_case('compare',modules)
        for name in ('Engine','Upstream','Bridge'):
            self.assertRegex(report,rf'(?m)^{name} ELF load bias: 0x[0-9a-f]+$')
        for module in modules:
            self.assertRegex(report,rf'(?m)^[0-9a-f]+-[0-9a-f]+ r-xp .*{re.escape(module.name)}$')
        command=[sys.executable,str(ROOT/'tools/android/symbolize-crash.py'),str(directory/'native-crash.txt'),
                 str(modules[0]),'--module','upstream','--addr2line','addr2line']
        result=subprocess.run(command,text=True,capture_output=True,timeout=8)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('CrashFixtureFault',result.stdout);self.assertIn('compare-module.c:',result.stdout)
        command[3]=str(modules[1])
        result=subprocess.run(command,text=True,capture_output=True,timeout=8)
        self.assertNotEqual(result.returncode,0);self.assertIn('SHA256',result.stderr)
    def test_other_thread_is_not_misreported_as_vm(self):
        _,report=self.run_case('worker')
        fault=re.search(r'Fault native tid: (\d+)',report)[1]
        vm=re.search(r'VM owner native tid: (\d+)',report)[1]
        self.assertNotEqual(fault,vm)
    def test_stack_overflow_uses_alternate_stack(self):
        self.run_case('overflow')
    def test_unavailable_proc_files_still_produce_a_report(self):
        _,report=self.run_case('no-proc')
        self.assertIn('Fault address: 0x1\n',report)
        self.assertIn('No readable caller frame.',report)
        self.assertIn('Memory maps unavailable.',report)
    def test_concurrent_breadcrumb_writer(self):
        _,report=self.run_case('concurrent')
        records=[line for line in report.splitlines() if line.startswith('T7 ')]
        self.assertTrue(records)
        self.assertTrue(all(re.fullmatch(r'T7 (cnfgwndtxt|preview)\._bp \+0x[0-9a-f]+ ip=0x[0-9a-f]+ opcode=0x[0-9a-f]+',line) for line in records))
    def test_crash_survives_multiple_normal_launches(self):
        directory,report=self.run_case('rotation')
        for _ in range(2):
            result=subprocess.run([str(self.binary),str(directory),'normal'],timeout=8)
            self.assertEqual(result.returncode,0)
            self.assertEqual((directory/'native-crash-previous.txt').read_text(),report)
            self.assertEqual((directory/'native-crash.txt').stat().st_size,0)

if __name__=='__main__': unittest.main()
