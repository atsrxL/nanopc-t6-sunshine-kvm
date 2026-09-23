# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the transformed pinned HTTP logging function against synthetic requests.
No real keys, certificates, input events, or network are used.
"""
import importlib.util
from pathlib import Path
import shutil
import re
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
    def test_synthetic_rtsp_raw_options_payload_not_logged(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++ compiler not present')
        changed=overlay.make_changes({'src/rtsp.cpp':self.original('src/rtsp.cpp')})['src/rtsp.cpp']
        begin=changed.index('  void print_msg(PRTSP_MESSAGE msg) {')
        end=changed.index('\n  }',begin)+4
        function=changed[begin:end]
        source='''#include <sstream>
#include <string>
#include <string_view>
using namespace std::literals;
std::ostringstream recorded;
#define BOOST_LOG(level) recorded
namespace rkmoon_sunshine { bool enabled() { return true; } }
constexpr int TYPE_RESPONSE=1;
struct option_t { const char *content; const char *option; option_t *next; };
struct message_t {
int type, payloadLength, sequenceNumber;
const char *payload, *protocol, *messageBuffer;
struct { struct { int statusCode; const char *statusString; } response;
struct { const char *command; const char *target; } request; } message;
option_t *options;
};
using PRTSP_MESSAGE=message_t*;
'''+function+'''
int main() {
option_t option={"SYNTHETIC_RTSP_OPTION", "Authorization", nullptr};
message_t msg{};
msg.payload="SYNTHETIC_RTSP_PAYLOAD";msg.payloadLength=22;
msg.protocol="RTSP/1.0";msg.messageBuffer="SYNTHETIC_RTSP_RAW";
msg.message.request.command="ANNOUNCE";
msg.message.request.target="SYNTHETIC_RTSP_TARGET";msg.options=&option;
print_msg(&msg);
msg.type=TYPE_RESPONSE;msg.message.response.statusCode=200;
msg.message.response.statusString="SYNTHETIC_RTSP_RESPONSE";
print_msg(&msg);
auto log=recorded.str();
return log.find("SYNTHETIC_")!=std::string::npos || log.find("redacted")==std::string::npos;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.cpp').write_text(source)
            subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True,capture_output=True)
            subprocess.run([str(p/'test')],check=True,capture_output=True)
    def test_input_logging_entry_guard(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++ compiler not present')
        changed=overlay.make_changes({'src/input.cpp':self.original('src/input.cpp')})['src/input.cpp']
        begin=changed.index('  void print(void *payload) {')
        end=changed.index('    auto header = (PNV_INPUT_HEADER) payload;',begin)
        # Compile the real patched entry guard with a sentinel downstream sink.
        # This checks guard dominance, not the entire upstream input parser.
        function=changed[begin:end]+'recorded << static_cast<const char*>(payload);\n}\n'
        source='''#include <sstream>
#include <string>
std::ostringstream recorded;
namespace rkmoon_sunshine { bool enabled() { return true; } }
'''+function+'''
int main() {
char keys[]="SYNTHETIC_DECRYPTED_KEYCODE_UNICODE";
print(keys);
return !recorded.str().empty();
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.cpp').write_text(source)
            subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True,capture_output=True)
            subprocess.run([str(p/'test')],check=True,capture_output=True)
    def test_unknown_control_and_ping_log_sites(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++ compiler not present')
        changed=overlay.make_changes({'src/stream.cpp':self.original('src/stream.cpp')})['src/stream.cpp']
        start=changed.index('      BOOST_LOG(debug)\n        << "type [Unknown]')
        end=changed.index(';',start)+1
        control=changed[start:end]
        pings=re.findall(r'        BOOST_LOG\(debug\) << "Received (?:ping \[v[12]\]|non-ping) \(payload redacted\)";',changed)
        self.assertEqual(len(pings),3)
        source='''#include <sstream>
#include <string>
#include <string_view>
using namespace std::literals;
std::ostringstream recorded;
#define BOOST_LOG(level) recorded
namespace util { struct hex { hex(int) {} std::string_view to_string_view() { return "unknown"; } }; }
int main() {
int type=1;
std::string payload="SYNTHETIC_CONTROL_BYTES",msg="SYNTHETIC_PING_BYTES";
'''+control+'\n'+'\n'.join(pings)+'''
auto log=recorded.str();
return log.find("SYNTHETIC_")!=std::string::npos || log.find("redacted")==std::string::npos;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.cpp').write_text(source)
            subprocess.run([compiler,'-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True,capture_output=True)
            subprocess.run([str(p/'test')],check=True,capture_output=True)
    def test_control_and_rtsp_payloads_redacted(self):
        changed=overlay.make_changes({p:self.original(p) for p in ['src/stream.cpp','src/rtsp.cpp','src/input.cpp']})
        self.assertNotIn('util::hex_vec(payload)',changed['src/stream.cpp'])
        self.assertNotIn('util::hex_vec(msg)',changed['src/stream.cpp'])
        self.assertIn('RKMoon RTSP message (content redacted)',changed['src/rtsp.cpp'])
        self.assertIn('Never log decoded input, even at verbose.',changed['src/input.cpp'])
