# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the transformed pinned HTTP logging function against synthetic requests.
No real keys, certificates, input events, or network are used.
"""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('overlay_redaction',ROOT/'tools/apply_sunshine.py')
overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)

class LogRedactionTests(unittest.TestCase):
    def original(self,path):
        repo=ROOT/'vendor/sunshine'
        if not (repo/'.git').exists():self.skipTest('pinned vendor checkout not present')
        return subprocess.check_output(['git','-C',str(repo),'show',overlay.PIN+':'+path],text=True)
    def test_synthetic_query_certificate_not_logged(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++ compiler not present')
        changed=overlay.make_changes({'src/nvhttp.cpp':self.original('src/nvhttp.cpp')})['src/nvhttp.cpp']
        begin=changed.index('  void print_req(')
        end=changed.index('\n  }',begin)+4
        function=changed[begin:end]
        source='''#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
using namespace std::literals;
std::ostringstream recorded;
#define BOOST_LOG(level) recorded
namespace rkmoon_sunshine { bool enabled() { return true; } }
namespace SimpleWeb {
template<class T> struct ServerBase { struct Request {
std::string method, path;
std::map<std::string,std::string> header;
std::map<std::string,std::string> parse_query_string() { return {{"rikey","SYNTHETIC_KEY_SENTINEL"},{"pin","SYNTHETIC_PIN_SENTINEL"},{"clientcert","SYNTHETIC_CERT_SENTINEL"}}; }
}; };
}
template<class T> struct tunnel { static constexpr auto to_string="TLS"sv; };
template<class T>
'''+function+'''
int main() {
auto req=std::make_shared<SimpleWeb::ServerBase<int>::Request>();
req->method="GET";
req->path="/launch?rikey=SYNTHETIC_KEY_SENTINEL";
req->header={{"Authorization","SYNTHETIC_AUTH_SENTINEL"}};
print_req<int>(req);
auto log=recorded.str();
return log.find("SYNTHETIC_")!=std::string::npos || log.find("redacted")==std::string::npos;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.cpp').write_text(source)
            subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True,capture_output=True)
            subprocess.run([str(p/'test')],check=True,capture_output=True)
        self.assertNotIn('BOOST_LOG(debug) << sess.client.cert;',changed)
    def test_control_and_rtsp_payloads_redacted(self):
        changed=overlay.make_changes({p:self.original(p) for p in ['src/stream.cpp','src/rtsp.cpp','src/input.cpp']})
        self.assertNotIn('util::hex_vec(payload)',changed['src/stream.cpp'])
        self.assertNotIn('util::hex_vec(msg)',changed['src/stream.cpp'])
        self.assertIn('RKMoon RTSP message (content redacted)',changed['src/rtsp.cpp'])
        self.assertIn('Never log decoded input, even at verbose.',changed['src/input.cpp'])
