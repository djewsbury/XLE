// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NodeGraph.h"
#include "CLIXAutoPtr.h"
#include "MarshalString.h"
#include "../ShaderParser/ShaderSignatureParser.h"
#include "../ShaderParser/Exceptions.h"
#include "../../Assets/IFileSystem.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../OSServices/FileSystemMonitor.h"
#include "../../Utility/StringUtils.h"

using namespace System::Collections::Generic;

namespace GUILayer
{
	public ref class ShaderFragment
    {
	public:
        property List<KeyValuePair<System::String^, NodeGraphSignature^>>^			Functions;
        property List<KeyValuePair<System::String^, UniformBufferSignature^>>^      UniformBuffers;
        property System::String^ Name;
        property System::String^ ExceptionString;

        event System::EventHandler^ ChangeEvent;

        void OnChange(Object^obj)
		{
			++_changeMarker;
			ChangeEvent(obj, System::EventArgs::Empty);
		}

        unsigned GetChangeMarker() { return _changeMarker; }

		ShaderFragment(System::String^ sourceFile);
		~ShaderFragment();

	private:
        clix::shared_ptr<OSServices::OnChangeCallback> _fileChangeCallback;
        unsigned _changeMarker;
    };

	namespace Internal
	{
		class ShaderFragmentChangeCallback : public OSServices::OnChangeCallback
		{
		public:
			virtual void    OnChange();
			ShaderFragmentChangeCallback(ShaderFragment^ shaderFragment, System::Threading::SynchronizationContext^ mainThread);
			virtual ~ShaderFragmentChangeCallback();
		private:
			msclr::gcroot<System::WeakReference^> _shaderFragment;
			msclr::gcroot<System::Threading::SynchronizationContext^> _mainThread;
		};

		void    ShaderFragmentChangeCallback::OnChange() 
		{
			auto frag = dynamic_cast<ShaderFragment^>(_shaderFragment->Target);
			if (frag)
				_mainThread->Post(gcnew System::Threading::SendOrPostCallback(frag, &ShaderFragment::OnChange), nullptr);
		}

		ShaderFragmentChangeCallback::ShaderFragmentChangeCallback(ShaderFragment^ shaderFragment, System::Threading::SynchronizationContext^ mainThread)
			: _shaderFragment(gcnew System::WeakReference(shaderFragment))
			, _mainThread(mainThread)
		{}

		ShaderFragmentChangeCallback::~ShaderFragmentChangeCallback()
		{}
	}

	ShaderFragment::ShaderFragment(System::String^ sourceFile)
    {
        _fileChangeCallback = nullptr;
        _changeMarker = 0;

            // 
            //      Load the given shader fragment, and extract the interface
            //      information.
            //
            //      The heavy lifting is done in native C++ code. So here,
            //      we're mostly just marshaling the data into a format
            //      that suits us.
            //
        Functions = gcnew List<KeyValuePair<System::String^, NodeGraphSignature^>>();
        UniformBuffers = gcnew List<KeyValuePair<System::String^, UniformBufferSignature^>>();

		std::string nativeFilename;

		size_t size = 0;
        std::unique_ptr<uint8[]> file;
		::Assets::FileSnapshot fileSnapshot;
        try
        {
			Name = System::IO::Path::GetFileNameWithoutExtension(sourceFile);
			nativeFilename = clix::marshalString<clix::E_UTF8>(sourceFile);
            file = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(nativeFilename, &size, &fileSnapshot);
        } catch (System::IO::IOException^) {

                //      Hit an exception! Most likely, the file just doesn't
                //      exist.
            ExceptionString = "Failed while opening file";

        } catch (System::ArgumentException^) {
			ExceptionString = "Failed while opening file (probably bad filename)";
		}
            
        if (file && size != 0) {

			auto srcCode = MakeStringSection((const char*)file.get(), (const char*)PtrAdd(file.get(), size));
            try {
				if (XlEqStringI(MakeFileNameSplitter(nativeFilename).Extension(), "graph")) {

					auto graphSyntax = GraphLanguage::ParseGraphSyntax(srcCode);
					for (const auto& subGraph:graphSyntax._subGraphs) {
						auto name = clix::marshalString<clix::E_UTF8>(MakeStringSection(subGraph.first).AsString());
						GUILayer::ConversionContext convContext;
						Functions->Add(KeyValuePair<System::String^, NodeGraphSignature^>(name, NodeGraphSignature::ConvertFromNative(subGraph.second._signature, convContext)));
					}

				} else {

					auto nativeSignature = ShaderSourceParser::ParseHLSL(srcCode);

					for (const auto& fn:nativeSignature._functions) {
						auto name = clix::marshalString<clix::E_UTF8>(MakeStringSection(fn.first).AsString());
						GUILayer::ConversionContext convContext;
						Functions->Add(KeyValuePair<System::String^, NodeGraphSignature^>(name, NodeGraphSignature::ConvertFromNative(fn.second, convContext)));
					}

					for (const auto& ps:nativeSignature._uniformBuffers) {
						auto name = clix::marshalString<clix::E_UTF8>(MakeStringSection(ps.first).AsString());
						UniformBuffers->Add(KeyValuePair<System::String^, UniformBufferSignature^>(name, UniformBufferSignature::ConvertFromNative(ps.second)));
					}
				}
            } catch (const ShaderSourceParser::Exceptions::ParsingFailure& ) {
                ExceptionString = "Failed during parsing. Look for compilation errors.";
            }
            
        }

        std::shared_ptr<Internal::ShaderFragmentChangeCallback> changeCallback(
            new Internal::ShaderFragmentChangeCallback(this, System::Threading::SynchronizationContext::Current));
		::Assets::MainFileSystem::TryMonitor(fileSnapshot, MakeStringSection(clix::marshalString<clix::E_UTF8>(sourceFile)), changeCallback);
        _fileChangeCallback = changeCallback;
    }

	ShaderFragment::~ShaderFragment()
	{
		delete Functions;
		delete UniformBuffers;
		delete Name;
		delete ExceptionString;
		_fileChangeCallback.reset();
	}

}

