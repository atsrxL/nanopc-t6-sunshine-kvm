// SPDX-License-Identifier: GPL-3.0-or-later
// The upstream CompatFetcher downloads a GFE version blacklist from the internet at
// startup. This client only talks to one dedicated host on the local network, so the
// fetcher is not built and every server version is accepted here. Unsupported servers
// still fail later in Session::validateLaunch() based on the server's own appversion.
#include "settings/compatfetcher.h"

bool CompatFetcher::isGfeVersionSupported(QString)
{
    return true;
}
