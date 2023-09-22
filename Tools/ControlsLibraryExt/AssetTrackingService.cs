// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using Sce.Atf;
using System.ComponentModel.Composition;

namespace ControlsLibraryExt
{
    [Export(typeof(IInitializable))]
    [Export(typeof(AssetTrackingService))]
    [Export(typeof(GUILayer.IShutdownWithEngine))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class AssetTrackingService : IInitializable, GUILayer.IShutdownWithEngine
    {
        public GUILayer.InvalidAssetList InvalidAssetList { get { return _invalidAssetList; } }

        void IInitializable.Initialize()
        {
            _invalidAssetList = new GUILayer.InvalidAssetList();
        }

        void GUILayer.IShutdownWithEngine.Shutdown()
        {
            _invalidAssetList.Dispose();
            _invalidAssetList = null;
        }

        private GUILayer.InvalidAssetList _invalidAssetList;
    }
}
