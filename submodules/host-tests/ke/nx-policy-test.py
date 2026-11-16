# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The data execution prevention policy and feature bits each NOEXECUTE boot option selects on x86
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
import argparse
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)

source = (root / 'ntos/ke/nxpolicy.c').read_text()
function = source[source.index('UCHAR\nNTAPI\nKiNxPolicyFromLoadOptions('):source.index('/* EOF */')]
ketypes = (root / 'sdk/include/ndk/amd64/ketypes.h').read_text() + (root / 'sdk/include/ndk/ketypes.h').read_text()
defines = '\n'.join(sorted(set(re.findall(r'^#define (?:KF_NX_(?:ENABLED|DISABLED)|NX_SUPPORT_POLICY_\w+)\s+\S+$', ketypes, re.M))))

fixture = r'''
#include <stdio.h>
#include <string.h>
typedef unsigned char UCHAR;
typedef unsigned int ULONG, *PULONG;
typedef const char *PCSTR;
#define NTAPI
#define _In_opt_
#define _Out_
@DEFINES@
@FUNCTION@
static int Failures;
static void Check(PCSTR Options, UCHAR Policy, ULONG Bits)
{
    ULONG Got = 0xFFFFFFFF;
    UCHAR Result = KiNxPolicyFromLoadOptions(Options, &Got);
    if (Result != Policy || Got != Bits)
    {
        printf("FAIL \"%s\": policy %u bits %#x, expected policy %u bits %#x\n",
               Options ? Options : "(null)", Result, Got, Policy, Bits);
        Failures++;
    }
}
int main(void)
{
    Check(NULL, NX_SUPPORT_POLICY_OPTIN, 0);
    Check("", NX_SUPPORT_POLICY_OPTIN, 0);
    Check("DEBUG SOS", NX_SUPPORT_POLICY_OPTIN, 0);
    Check("NOEXECUTE=ALWAYSON", NX_SUPPORT_POLICY_ALWAYSON, KF_NX_ENABLED);
    Check("NOEXECUTE=OPTOUT", NX_SUPPORT_POLICY_OPTOUT, KF_NX_ENABLED);
    Check("NOEXECUTE=OPTIN", NX_SUPPORT_POLICY_OPTIN, KF_NX_ENABLED);
    Check("NOEXECUTE", NX_SUPPORT_POLICY_OPTIN, KF_NX_ENABLED);
    Check("NOEXECUTE=ALWAYSOFF", NX_SUPPORT_POLICY_ALWAYSOFF, KF_NX_DISABLED);
    Check("DEBUG NOEXECUTE=ALWAYSOFF SOS", NX_SUPPORT_POLICY_ALWAYSOFF, KF_NX_DISABLED);
    Check("EXECUTE", NX_SUPPORT_POLICY_ALWAYSOFF, KF_NX_DISABLED);
    if (!Failures)
        printf("nx-policy: all options select the expected policy\n");
    return Failures != 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'nx.c').write_text(fixture.replace('@DEFINES@', defines).replace('@FUNCTION@', function))
    subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(path / 'nx.c'), '-o', str(path / 'nx')], check=True)
    subprocess.run([str(path / 'nx')], check=True)
