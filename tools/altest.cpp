// Test harness: resolve the plugin's IDs from Address Library files given on the command line.
#include "AddressLibrary.h"
#include <cstdio>

int wmain(int argc, wchar_t** argv)
{
	const std::uint32_t ids[] = { 42354, 410199, 410200, 373402 };
	int rc = 0;
	for (int a = 1; a < argc; ++a) {
		AddressLibrary al;
		std::uint64_t off[4];
		const bool ok = al.Resolve(argv[a], ids, 4, off);
		std::printf("%ls format=%u ok=%d err=%s", argv[a], al.format, ok ? 1 : 0, ok ? "" : al.error);
		for (int i = 0; i < 4; ++i) {
			std::printf(" %u=0x%llx", ids[i], static_cast<unsigned long long>(off[i]));
		}
		std::printf("\n");
		rc |= ok ? 0 : 1;
	}
	return rc;
}
