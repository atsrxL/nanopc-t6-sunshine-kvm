#!/usr/bin/env python3
"""Extend the production HTTP smoke test with absent-device display metadata."""
import os
from pathlib import Path

source = Path(__file__).with_name('test_minimal_startup.py')
text = source.read_text()
anchor = '                host_uuid = tree.findtext("uniqueid")'
assert text.count(anchor) == 1
checks = '''                expected = {
                    "RKMoonDisplayVersion": "1",
                    "RKMoonDisplayStatus": "unavailable",
                    "RKMoonDisplayWidth": "0",
                    "RKMoonDisplayHeight": "0",
                    "RKMoonDisplayFpsX100": "0",
                }
                for field, value in expected.items():
                    assert len(tree.findall(field)) == 1, field
                    assert tree.findtext(field) == value, field
                print("production_http_display_version1_unavailable_zero=pass")
'''
os.environ['RKMOON_VIDEO_DEVICE'] = '/nonexistent-rkmoon-display-test-device'
exec(compile(text.replace(anchor, checks+anchor), str(source), 'exec'), {'__name__': '__main__', '__file__': str(source)})
