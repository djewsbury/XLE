// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformAccelerator.h"
#include "SimpleModelDeform.h"
#include "DeformAcceleratorInternal.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelImmutableData.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../../Utility/ArithmeticUtils.h"
#include <vector>


namespace RenderCore { namespace Techniques
{
    class DeformAcceleratorPool : public IDeformAcceleratorPool
    {
    public:
        std::shared_ptr<DeformAccelerator> CreateDeformAccelerator(
            StringSection<> initializer,
            const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold) override;
        void DestroyAccelerator(DeformAccelerator&) override;

        std::vector<std::shared_ptr<IDeformOperation>> GetOperations(DeformAccelerator& accelerator, uint64_t typeId) override;
        void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) override;
        void ReadyInstances(IThreadContext&) override;
        void OnFrameBarrier() override;

        VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx) const override;

        DeformAcceleratorPool(std::shared_ptr<IDevice>);
        ~DeformAcceleratorPool();

    private:
        DeformOperationFactory _opFactory;

        std::vector<std::shared_ptr<DeformAccelerator>> _accelerators;
        std::shared_ptr<IDevice> _device;

        void ExecuteInstance(IThreadContext& threadContext, DeformAccelerator& a, unsigned instanceIdx);
    };

    class DeformAccelerator
    {
    public:
        std::vector<uint64_t> _instanceStates;
        unsigned _minActiveInstance = ~0u;
        unsigned _maxActiveInstance = 0;

        std::vector<Internal::DeformOp> _deformOps;

        std::shared_ptr<IResource> _dynVB;
        std::vector<uint8_t> _deformStaticDataInput;
		std::vector<uint8_t> _deformTemporaryBuffer;
        
        #if defined(_DEBUG)
            DeformAcceleratorPool* _containingPool = nullptr;
        #endif
    };

    std::shared_ptr<DeformAccelerator> DeformAcceleratorPool::CreateDeformAccelerator(
        StringSection<> initializer,
        const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold)
    {
        auto deformAttachments = _opFactory.CreateDeformOperations(initializer, modelScaffold);
        auto newAccelerator = std::make_shared<DeformAccelerator>();
        newAccelerator->_instanceStates.resize(8);
        #if defined(_DEBUG)
            newAccelerator->_containingPool = this;
        #endif

        ////////////////////////////////////////////////////////////////////////////////////
        // Build deform streams

        unsigned preDeformStaticDataVBIterator = 0;
		unsigned deformTemporaryVBIterator = 0;
		unsigned postDeformVBIterator = 0;
		std::vector<Internal::SourceDataTransform> deformStaticLoadDataRequests;

		std::vector<Internal::NascentDeformStream> geoDeformStreams;
		std::vector<Internal::NascentDeformStream> skinControllerDeformStreams;
        geoDeformStreams.reserve(modelScaffold->ImmutableData()._geoCount);

        for (unsigned geo=0; geo<modelScaffold->ImmutableData()._geoCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._geos[geo];

            unsigned vertexCount = rg._vb._size / rg._vb._ia._vertexStride;
			auto deform = Internal::BuildNascentDeformStream(
				deformAttachments, geo, vertexCount, 
				preDeformStaticDataVBIterator, deformTemporaryVBIterator, postDeformVBIterator);

            newAccelerator->_deformOps.insert(newAccelerator->_deformOps.end(), deform._deformOps.begin(), deform._deformOps.end());

            deformStaticLoadDataRequests.insert(
				deformStaticLoadDataRequests.end(),
				deform._staticDataLoadRequests.begin(), deform._staticDataLoadRequests.end());
            geoDeformStreams.push_back(std::move(deform));
        }

        skinControllerDeformStreams.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
        for (unsigned geo=0; geo<modelScaffold->ImmutableData()._boundSkinnedControllerCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._boundSkinnedControllers[geo];

            unsigned vertexCount = rg._vb._size / rg._vb._ia._vertexStride;
			auto deform = Internal::BuildNascentDeformStream(
				deformAttachments, geo + (unsigned)modelScaffold->ImmutableData()._geoCount, vertexCount,
				preDeformStaticDataVBIterator, deformTemporaryVBIterator, postDeformVBIterator);

            newAccelerator->_deformOps.insert(newAccelerator->_deformOps.end(), deform._deformOps.begin(), deform._deformOps.end());

            deformStaticLoadDataRequests.insert(
				deformStaticLoadDataRequests.end(),
				deform._staticDataLoadRequests.begin(), deform._staticDataLoadRequests.end());
            skinControllerDeformStreams.push_back(std::move(deform));
        }

        ////////////////////////////////////////////////////////////////////////////////////

        // Create the dynamic VB and assign it to all of the slots it needs to go to
		if (postDeformVBIterator) {
			newAccelerator->_dynVB = _device->CreateResource(
				CreateDesc(
					BindFlag::VertexBuffer,
					CPUAccess::WriteDynamic, GPUAccess::Read,
					LinearBufferDesc::Create(postDeformVBIterator),
					"ModelRendererDynVB"));

			/*for (auto&g:_geos)
				for (unsigned s=0; s<g->_vertexStreamCount; ++s)
					if (!g->_vertexStreams[s]._resource)
						g->_vertexStreams[s]._resource = _dynVB;

			for (auto&g:_boundSkinnedControllers)
				for (unsigned s=0; s<g->_vertexStreamCount; ++s)
					if (!g->_vertexStreams[s]._resource)
						g->_vertexStreams[s]._resource = _dynVB;*/
		}

		if (preDeformStaticDataVBIterator) {
			newAccelerator->_deformStaticDataInput = Internal::GenerateDeformStaticInput(
				*modelScaffold,
				MakeIteratorRange(deformStaticLoadDataRequests),
				preDeformStaticDataVBIterator);
		}

		if (deformTemporaryVBIterator) {
			newAccelerator->_deformTemporaryBuffer.resize(deformTemporaryVBIterator, 0);
		}

        return newAccelerator;
    }

    void DeformAcceleratorPool::DestroyAccelerator(DeformAccelerator& accelerator)
    {
        auto i = std::find_if(_accelerators.begin(), _accelerators.begin(), [&accelerator](const auto& a) { return a.get() == &accelerator; });
        assert(i!=_accelerators.end());
        if (i!=_accelerators.end())
            _accelerators.erase(i);            
    }

    std::vector<std::shared_ptr<IDeformOperation>> DeformAcceleratorPool::GetOperations(DeformAccelerator& accelerator, size_t typeId)
    {
        #if defined(_DEBUG)
            assert(accelerator._containingPool == this);
        #endif
        std::vector<std::shared_ptr<IDeformOperation>> result;
        for (const auto&i:accelerator._deformOps)
            if (i._deformOp->QueryInterface(typeId))
                result.push_back(i._deformOp);
        return result;
    }

    void DeformAcceleratorPool::EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx)
    {
        assert(instanceIdx!=~0u);
        #if defined(_DEBUG)
            assert(accelerator._containingPool == this);
        #endif
        auto field = instanceIdx / 64;
        if (accelerator._instanceStates.size() <= field)
            accelerator._instanceStates.resize(field+1, 0);
        accelerator._instanceStates[field] |= instanceIdx & (64-1);
        accelerator._minActiveInstance = std::min(accelerator._minActiveInstance, instanceIdx);
        accelerator._maxActiveInstance = std::min(accelerator._maxActiveInstance, instanceIdx);
    }

    void DeformAcceleratorPool::ExecuteInstance(IThreadContext& threadContext, DeformAccelerator& a, unsigned instanceIdx)
    {
        assert(instanceIdx!=~0u);
        auto& metalContext = *Metal::DeviceContext::Get(threadContext);

		auto* res = (Metal::Resource*)a._dynVB->QueryInterface(typeid(Metal::Resource).hash_code());
		assert(res);

		Metal::ResourceMap map(metalContext, *res, Metal::ResourceMap::Mode::WriteDiscardPrevious);
		auto dst = map.GetData();

		auto staticDataPartRange = MakeIteratorRange(a._deformStaticDataInput);
		auto temporaryDeformRange = MakeIteratorRange(a._deformTemporaryBuffer);

		for (const auto&d:a._deformOps) {

			IDeformOperation::VertexElementRange inputElementRanges[16];
			assert(d._inputElements.size() <= dimof(inputElementRanges));
			for (unsigned c=0; c<d._inputElements.size(); ++c) {
				if (d._inputElements[c]._vbIdx == Internal::VB_StaticData) {
					inputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(staticDataPartRange.begin()), d._inputElements[c]._offset), AsPointer(staticDataPartRange.end())),
						d._inputElements[c]._stride, d._inputElements[c]._format);
				} else {
					assert(d._inputElements[c]._vbIdx == Internal::VB_TemporaryDeform);
					inputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(temporaryDeformRange.begin()), d._inputElements[c]._offset), AsPointer(temporaryDeformRange.end())),
						d._inputElements[c]._stride, d._inputElements[c]._format);
				}
			}

			auto outputPartRange = dst;
			assert(outputPartRange.begin() < outputPartRange.end() && PtrDiff(outputPartRange.end(), dst.begin()) <= ptrdiff_t(dst.size()));

			IDeformOperation::VertexElementRange outputElementRanges[16];
			assert(d._outputElements.size() <= dimof(outputElementRanges));
			for (unsigned c=0; c<d._outputElements.size(); ++c) {
				if (d._outputElements[c]._vbIdx == Internal::VB_PostDeform) {
					outputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(outputPartRange.begin(), d._outputElements[c]._offset), outputPartRange.end()),
						d._outputElements[c]._stride, d._outputElements[c]._format);
				} else {
					assert(d._outputElements[c]._vbIdx == Internal::VB_TemporaryDeform);
					outputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(temporaryDeformRange.begin()), d._outputElements[c]._offset), AsPointer(temporaryDeformRange.end())),
						d._outputElements[c]._stride, d._outputElements[c]._format);
				}
			}

			// Execute the actual deform op
			d._deformOp->Execute(
				instanceIdx,
				MakeIteratorRange(inputElementRanges, &inputElementRanges[d._inputElements.size()]),
				MakeIteratorRange(outputElementRanges, &outputElementRanges[d._outputElements.size()]));
		}
    }

    void DeformAcceleratorPool::ReadyInstances(IThreadContext& threadContext)
    {
        for (const auto&a:_accelerators) {
            if (a->_maxActiveInstance <= a->_minActiveInstance) continue;
            auto fMin = a->_minActiveInstance / 64, fMax = a->_maxActiveInstance / 64;
            for (auto f=fMin; f<=fMax; ++f) {
                auto active = a->_instanceStates[f];
                while (active) {
                    auto bit = xl_ctz8(active);
                    active ^= 1ull<<uint64_t(bit);
                    
                    auto instance = f*64+bit;
                    ExecuteInstance(threadContext, *a, instance);
                }
            }
        }
    }

    VertexBufferView DeformAcceleratorPool::GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceId) const
    {
        return {};
    }

    inline void DeformAcceleratorPool::OnFrameBarrier()
    {
        for (auto& a:_accelerators) {
            for (auto& c:a->_instanceStates) 
                c = 0;
            a->_minActiveInstance = ~0u;
            a->_maxActiveInstance = 0u;
        }
    }

    DeformAcceleratorPool::DeformAcceleratorPool(std::shared_ptr<IDevice> device)
    : _device(std::move(device))
    {}
    DeformAcceleratorPool::~DeformAcceleratorPool() {}

    static uint64_t s_nextDeformAcceleratorPool = 1;
    IDeformAcceleratorPool::IDeformAcceleratorPool() 
    : _guid(s_nextDeformAcceleratorPool++)
    {}
    IDeformAcceleratorPool::~IDeformAcceleratorPool() {}

    std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device)
    {
        return std::make_shared<DeformAcceleratorPool>(std::move(device));
    }

}}
