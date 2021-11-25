// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Utility/StringUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class IDevice; class IThreadContext; class VertexBufferView; }
namespace RenderCore { namespace Assets { class ModelScaffold; }}

namespace RenderCore { namespace Techniques
{
    class DeformAccelerator;
    class ICPUDeformOperator;
    struct RendererGeoDeformInterface;

    class IDeformAcceleratorPool
    {
    public:
        virtual std::shared_ptr<DeformAccelerator> CreateDeformAccelerator(
            StringSection<> initializer,
            const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold) = 0;

        virtual IteratorRange<const RendererGeoDeformInterface*> GetRendererGeoInterface(DeformAccelerator& accelerator) const = 0;

        virtual std::vector<std::shared_ptr<ICPUDeformOperator>> GetOperations(DeformAccelerator& accelerator, size_t typeId) = 0;
        virtual void EnableInstance(DeformAccelerator& accelerator, unsigned instanceIdx) = 0;
        virtual void ReadyInstances(IThreadContext&) = 0;
        virtual void OnFrameBarrier() = 0;

        virtual VertexBufferView GetOutputVBV(DeformAccelerator& accelerator, unsigned instanceIdx) const = 0;

        unsigned GetGUID() const { return _guid; }

        IDeformAcceleratorPool();
        virtual ~IDeformAcceleratorPool();
    private:
        uint64_t _guid;
    };

    std::shared_ptr<IDeformAcceleratorPool> CreateDeformAcceleratorPool(std::shared_ptr<IDevice> device);

}}
