
#include "DirectXTex/DirectXTex/DirectXTexP.h"

namespace DirectX
{
	// Stubbed out to allow a cut-down DirectXTex without the decompression code.
	// This will cause ScratchImage::IsAlphaAllOpaque() to not return a reliable result
	//		-- but that's not an issue for our use cases
	struct Image;
	bool _IsAlphaAllOpaqueBC(_In_ const Image&) noexcept { assert(0); return false; }
};
