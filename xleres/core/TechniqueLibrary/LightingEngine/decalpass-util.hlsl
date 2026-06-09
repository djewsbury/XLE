
#if DECAL_PASS

	[[vk::input_attachment_index(0)]] SubpassInput<float> DepthTexture : register(t0, space5);
	Texture2D RefractionBuffer : register(t1, space5);

	float GetDecalDepth() { return DepthTexture.SubpassLoad(); }

	bool CompareDecalDepth(float4 svPosition)
	{
		const float depthThreshold = 1e-4f;
		return abs(svPosition.z - GetDecalDepth()) < depthThreshold;		// ReverseZ
	}

#else

	float GetDecalDepth() { return 0.f; }		// ReverseZ
	bool CompareDecalDepth(float4 svPosition) { return true; }

#endif