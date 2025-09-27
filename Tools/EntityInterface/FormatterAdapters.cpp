// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FormatterAdapters.h"
#include "EntityInterface.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/Continuation.h"
#include "../../Formatters/FormatterUtils.h"

namespace EntityInterface
{

    template<typename Formatter> 
		class TextFormatterAdapter : public Formatters::IDynamicInputFormatter
	{
	public:
		virtual Formatters::FormatterBlob PeekNext() override { return _fmttr.PeekNext(); }

		virtual bool TryBeginElement() override { return _fmttr.TryBeginElement(); }
		virtual bool TryEndElement() override { return _fmttr.TryEndElement(); }
		virtual bool TryKeyedItem(StringSection<>& name) override { return _fmttr.TryKeyedItem(name); }
		virtual bool TryKeyedItem(uint64_t& name) override
		{
			return Formatters::TryKeyedItem(_fmttr, name);
		}

		virtual bool TryStringValue(StringSection<>& value) override
		{ 
			return _fmttr.TryStringValue(value);
		}

		virtual bool TryRawValue(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type) override
		{ 
			StringSection<> strSection;
			auto res = _fmttr.TryStringValue(strSection);
			if (res) {
				value = {strSection.begin(), strSection.end()};
				type = {ImpliedTyping::TypeOf<const char*>()._type, (uint16_t)strSection.size(), ImpliedTyping::TypeHint::String};
			}
			return res;
		}

		virtual bool TryCastValue(IteratorRange<void*> destinationBuffer, const ImpliedTyping::TypeDesc& type) override
		{
			StringSection<> strSection;
			auto res = _fmttr.TryStringValue(strSection);
			if (res)
				return ImpliedTyping::ConvertFullMatch(strSection, destinationBuffer, type);
			return false;
		}

		virtual void SkipValueOrElement() override
		{
			Formatters::SkipValueOrElement(_fmttr);
		}

		virtual Formatters::StreamLocation GetLocation() const override { return _fmttr.GetLocation(); }
		virtual ::Assets::DependencyValidation GetDependencyValidation() const override { return _cfgFile->GetDependencyValidation(); }

		TextFormatterAdapter(
			std::shared_ptr<::Assets::ConfigFileContainer<Formatter>> cfgFile,
			StringSection<typename Formatter::value_type> internalSection)
		: _cfgFile(cfgFile)
		{
			if (!internalSection.IsEmpty()) {
				VLA(typename Formatter::value_type, internalSectionCopy, internalSection.size());
				auto* c2 = &internalSectionCopy[0];
				for (auto c:internalSection) *c2++ = (c=='/')?':':c;
				_fmttr = _cfgFile->GetFormatter(MakeStringSection(internalSectionCopy, internalSectionCopy+internalSection.size()));
			} else {
				_fmttr = _cfgFile->GetRootFormatter();
			}
		}
	private:
		std::shared_ptr<::Assets::ConfigFileContainer<Formatter>> _cfgFile;
		Formatter _fmttr;
	};

    std::shared_ptr<Formatters::IDynamicInputFormatter> CreateDynamicFormatter(
        std::shared_ptr<::Assets::ConfigFileContainer<>> cfgFile,
        StringSection<> internalSection)
    {
        return std::make_shared<TextFormatterAdapter<decltype(cfgFile->GetRootFormatter())>>(std::move(cfgFile), internalSection);
    }

    class TextEntityDocument : public IEntityDocument
	{
	public:
		virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> internalPoint) override
		{
			if (!_srcFile || ::Assets::IsInvalidated(*_srcFile))
				_srcFile = ::Assets::GetAssetMarkerPtr<::Assets::ConfigFileContainer<>>(_src);

			std::promise<std::shared_ptr<Formatters::IDynamicInputFormatter>> promise;
			auto result = promise.get_future();
			::Assets::WhenAll(_srcFile).ThenConstructToPromise(
				std::move(promise),
				[ip=internalPoint.AsString()](auto cfgFileContainer) {
					return EntityInterface::CreateDynamicFormatter(std::move(cfgFileContainer), ip);
				});
			return result;
		}

		virtual const ::Assets::DependencyValidation& GetDependencyValidation() const override
		{
			return _srcFile->GetDependencyValidation();
		}

		virtual const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const override
		{
			return _directorySearchRules;
		}

		virtual void Lock() override { _lock = std::unique_lock<Threading::Mutex>{_readMutex}; }
		virtual bool TryLock() override 
		{
			_lock = std::unique_lock<Threading::Mutex>{_readMutex, std::defer_lock};
			return _lock.try_lock();
		}
		virtual void Unlock() override { _lock = {}; }

		TextEntityDocument(std::string src) 
		: _src(src)
		{
			_directorySearchRules.SetBaseFile(_src);
		}

	private:
		Threading::Mutex _readMutex;
		std::unique_lock<Threading::Mutex> _lock;
		std::string _src;
		::Assets::PtrToMarkerPtr<::Assets::ConfigFileContainer<>> _srcFile;
		::Assets::DirectorySearchRules _directorySearchRules;
	};

	std::shared_ptr<IEntityDocument> CreateTextEntityDocument(StringSection<> filename)
	{
		return std::make_shared<TextEntityDocument>(filename.AsString());
	}

	class MemoryStreamTextFormatterAdapter : public Formatters::IDynamicInputFormatter
	{
	public:
		virtual Formatters::FormatterBlob PeekNext() override { return _fmttr.PeekNext(); }

		virtual bool TryBeginElement() override { return _fmttr.TryBeginElement(); }
		virtual bool TryEndElement() override { return _fmttr.TryEndElement(); }
		virtual bool TryKeyedItem(StringSection<>& name) override { return _fmttr.TryKeyedItem(name); }
		virtual bool TryKeyedItem(uint64_t& name) override 
		{
			return Formatters::TryKeyedItem(_fmttr, name);
		}

		virtual bool TryStringValue(StringSection<>& value) override
		{ 
			return _fmttr.TryStringValue(value);
		}

		virtual bool TryRawValue(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type) override
		{ 
			StringSection<> strSection;
			auto res = _fmttr.TryStringValue(strSection);
			if (res) {
				value = {strSection.begin(), strSection.end()};
				type = {ImpliedTyping::TypeOf<const char*>()._type, (uint16_t)strSection.size(), ImpliedTyping::TypeHint::String};
			}
			return res;
		}

		virtual bool TryCastValue(IteratorRange<void*> destinationBuffer, const ImpliedTyping::TypeDesc& type) override
		{
			StringSection<> strSection;
			auto res = _fmttr.TryStringValue(strSection);
			if (res)
				return ImpliedTyping::ConvertFullMatch(strSection, destinationBuffer, type);
			return false;
		}

		virtual void SkipValueOrElement() override
		{
			Formatters::SkipValueOrElement(_fmttr);
		}

		virtual Formatters::StreamLocation GetLocation() const override { return _fmttr.GetLocation(); }
		virtual ::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }

		#if __cplusplus >= 202002L
			MemoryStreamTextFormatterAdapter(std::stringstream&& stream, ::Assets::DependencyValidation&& depVal)
			: _stream(std::move(stream))
			, _depVal(std::move(depVal))
			{
				_fmttr = Formatters::TextStreamMarker<char>{MakeIteratorRange(_stream.view().begin(), _stream.view().end()), _depVal};
			}
		#else
			MemoryStreamTextFormatterAdapter(std::string&& stream, ::Assets::DependencyValidation&& depVal)
			: _stream(std::move(stream))
			, _depVal(std::move(depVal))
			{
				_fmttr = Formatters::TextStreamMarker<char>{MakeIteratorRange(_stream.begin(), _stream.end()), _depVal};
			}
		#endif

	private:
		#if __cplusplus >= 202002L
			std::stringstream _stream;
		#else
			std::string _stream;
		#endif
		Formatters::TextInputFormatter<> _fmttr;
		::Assets::DependencyValidation _depVal;
	};

	std::shared_ptr<Formatters::IDynamicInputFormatter> CreateDynamicFormatter(
        std::stringstream&& formatter,
		::Assets::DependencyValidation&& depVal)
	{
		#if __cplusplus >= 202002L
			return std::make_shared<MemoryStreamTextFormatterAdapter>(std::move(formatter), std::move(depVal));
		#else
			return std::make_shared<MemoryStreamTextFormatterAdapter>(formatter.str(), std::move(depVal));
		#endif
	}
}
