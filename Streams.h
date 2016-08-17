#ifndef RUST_STREAMS_H
#define RUST_STREAMS_H

#include<tuple>

#if defined _MSC_VER
#include "Optional/optional.hpp"
# else
#include <experimental/optional>
#endif

namespace streams {

	template<typename T>
	using Optional = std::experimental::optional<T>;

	using std::experimental::nullopt;

	template<typename... Args>
	using Tuple = std::tuple<Args...>;



	template <typename DerivedStreamExtractor>
	struct StreamExtractor {
		// TODO: noexcept?
		auto get() {
			return static_cast<DerivedStreamExtractor*>(this)->get_impl();
		}

		// TODO: noexcept?
		bool advance() {
			return static_cast<DerivedStreamExtractor*>(this)->advance_impl();
		}
	};

	template <typename IteratorType>
	struct SequenceStreamExtractor : StreamExtractor<SequenceStreamExtractor<IteratorType>> {
		SequenceStreamExtractor(IteratorType b, IteratorType e) : current(b), next(b), begin(b), end(e) {}

		IteratorType current;
		IteratorType next;
		const IteratorType begin;
		const IteratorType end;

		auto get_impl() {
			return current;
		}

		bool advance_impl() {
			if (next != end) {
				current = next++;
				return true;
			} else {
				return false;
			}
		}
	};

	// TODO: templated size_t counter + static_assertion?
	template<typename ExtractorType>
	struct SkipFirstStreamExtractor : StreamExtractor<SkipFirstStreamExtractor<ExtractorType>> {
		SkipFirstStreamExtractor(ExtractorType extractor, size_t count) : source(extractor), skipCount(count) {}

		ExtractorType source;
		size_t skipCount;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			while (skipCount != 0) {
				--skipCount;
				if (!source.advance()) {
					return false;
				}
			}
			return source.advance();
		}

	};

	template<typename ExtractorType, typename Predicate>
	struct SkipWhileStreamExtractor : StreamExtractor<SkipWhileStreamExtractor<ExtractorType, Predicate>> {
		SkipWhileStreamExtractor(ExtractorType extractor, Predicate&& predicate) : source(extractor), predicate(std::forward<Predicate>(predicate)) {}

		ExtractorType source;
		Predicate predicate;
		bool skipping = true;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (skipping) {
				while (skipping && source.advance()) {
					skipping = predicate(*source.get());
				}
				return !skipping; // depleted stream : skipping == true
			} else {
				return source.advance();
			}
		}

	};

	template<typename ExtractorType>
	struct TakeStreamExtractor : StreamExtractor<TakeStreamExtractor<ExtractorType>> {
		TakeStreamExtractor(ExtractorType extractor, size_t count) : source(extractor), limit(count) {}

		ExtractorType source;
		size_t limit;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (limit != 0) {
				--limit;
				return source.advance();
			}
			return false;
		}

	};

	template<typename ExtractorType, typename Predicate>
	struct TakeWhileStreamExtractor : StreamExtractor<TakeWhileStreamExtractor<ExtractorType, Predicate>> {
		TakeWhileStreamExtractor(ExtractorType extractor, Predicate&& predicate) : source(extractor), predicate(std::forward<Predicate>(predicate)) {}

		ExtractorType source;
		Predicate predicate;
		bool taking = true;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			taking &= taking && source.advance() && predicate(*source.get());
			return taking;
		}

	};


	template<typename ExtractorType, typename Predicate>
	struct FilterStreamExtractor : StreamExtractor<FilterStreamExtractor<ExtractorType, Predicate>> {
		FilterStreamExtractor(ExtractorType extractor, Predicate&& p) : source(extractor), predicate(std::forward<Predicate>(p)) {}

		ExtractorType source;
		Predicate predicate;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (!source.advance()) {
				return false;
			}
			auto element = source.get();
			while (!predicate(*element)) {
				if (source.advance()) {
					element = source.get();
				} else {
					return false;
				}
			}
			return true;
		}

	};


	template<typename ExtractorType, typename Transform>
	struct MapStreamExtractor : StreamExtractor<MapStreamExtractor<ExtractorType, Transform>> {
		MapStreamExtractor(ExtractorType sourceExtractor, Transform&& transform) : source(sourceExtractor), transformer(std::forward<Transform>(transform)) {}

		ExtractorType source;
		Transform transformer;

		//using InputType = decltype(std::declval<ExtractorType>().get());
		//using OutputType = decltype(std::declval<Mapper>()(*std::declval<InputType>()))
		//OutputType value;

		// TODO: consider using shared_ptr here
        decltype(transformer(*std::declval<decltype(source.get())>())) value {};

		auto get_impl() {
			value = transformer(*source.get());
			return &value;
		}

		bool advance_impl() {
			return source.advance();
		}

	};

	// This should be optimized utilizing r- and l- return value from transformer
	// bad: InnerCollection is a copy
	// bad: placement new?
	// dont like the implementation at all...
	template<typename ExtractorType, typename Transform>
	struct FlatMapStreamExtractor : StreamExtractor<FlatMapStreamExtractor<ExtractorType, Transform>> {
		FlatMapStreamExtractor(ExtractorType sourceExtractor, Transform&& transform) : source(sourceExtractor), transformer(std::forward<Transform>(transform)) {}

		ExtractorType source;
		Transform transformer;

        using val = std::decay_t<decltype(source.get())>;
        using InnerCollection = decltype(transformer(*std::declval<val>()));
        InnerCollection innerCollection{};
        SequenceStreamExtractor<decltype(std::begin(innerCollection))> sequence{ std::begin(innerCollection), std::end(innerCollection) };


		auto get_impl() {
			return sequence.get();
		}

		bool advance_impl() {
            if (!sequence.advance()) {
                if (source.advance()) {
                    innerCollection = transformer(*source.get());
					new(&sequence) SequenceStreamExtractor<decltype(std::begin(innerCollection))> { std::begin(innerCollection), std::end(innerCollection) };
                    return advance_impl();
                }
                else {
                    return false;
                }
            }
            else {
                return true;
            }
		}

	};


	template<typename ExtractorType, typename Inspector>
	struct InspectStreamExtractor : StreamExtractor<InspectStreamExtractor<ExtractorType, Inspector>> {
		InspectStreamExtractor(ExtractorType extractor, Inspector&& inspector) : source(extractor), inspector(std::forward<Inspector>(inspector)) {}

		ExtractorType source;
		Inspector inspector;

		auto get_impl() {
			return source.get();
		}

		bool advance_impl() {
			if (source.advance()) {
				inspector(*source.get());
				return true;
			}
			return false;
		}

	};


	template<typename ExtractorType, typename Inspector>
	struct SpyStreamExtractor : StreamExtractor<SpyStreamExtractor<ExtractorType, Inspector>> {
		SpyStreamExtractor(ExtractorType extractor, Inspector&& inspector) : source(extractor), inspector(std::forward<Inspector>(inspector)) {}

		ExtractorType source;
		Inspector inspector;

		auto get_impl() {
			auto value = source.get();
			inspector(*value);
			return value;
		}

		bool advance_impl() {
			return source.advance();
		}

	};


	template<typename T>
	struct Enumerated {
		size_t i;
        std::decay_t<T> v;
		Enumerated& operator = (const Enumerated&) = default;
	};

	template<typename T>
    bool operator == (const Enumerated<T>& lhs, const Enumerated<T>& rhs) {
		return lhs.i == rhs.i && lhs.v == rhs.v;
	}

	// TODO: here, object from source is copied. Try to avoid it 
	template<typename ExtractorType>
	struct EnumerateStreamExtractor : StreamExtractor<EnumerateStreamExtractor<ExtractorType>> {
		EnumerateStreamExtractor(ExtractorType extractor, size_t counter = 0) : source(extractor), counter(counter){}

		ExtractorType source;
        size_t counter;
        Enumerated<std::decay_t<decltype(*source.get())>> value {counter, {}};

		auto get_impl() {
            value = {counter - 1, *source.get()};
            return &value;
		}

		bool advance_impl() {
            ++counter;
			return source.advance();
		}

	};


	template<typename ExtractorType>
	struct EnumerateTupleStreamExtractor : StreamExtractor<EnumerateTupleStreamExtractor<ExtractorType>> {
		EnumerateTupleStreamExtractor(ExtractorType extractor, size_t counter = 0) : source(extractor), counter(counter) {}

		ExtractorType source;
		size_t counter;
		Tuple<size_t, std::decay_t<decltype(*source.get())>> value{ counter, {} };

		auto get_impl() {
			value = { counter - 1, *source.get() };
			return &value;
		}

		bool advance_impl() {
			++counter;
			return source.advance();
		}

	};


	template<typename ExtractorType, typename ExtractorOtherType>
	struct ChainStreamExtractor : StreamExtractor<ChainStreamExtractor<ExtractorType, ExtractorOtherType>> {
        ChainStreamExtractor(ExtractorType extractor, ExtractorOtherType other) : first(extractor), next(other){}

		ExtractorType first;
		ExtractorOtherType next;
		bool firstHaveElements = true;

		auto get_impl() {
			if (firstHaveElements) {
				return first.get();
			} else {
				return next.get();
			}
		}

		bool advance_impl() {
			if (firstHaveElements && (firstHaveElements = first.advance())) {
				return true;
			}
			return next.advance();
		}

	};


	template<typename ExtractorType, typename ExtractorOtherType>
	struct ZipStreamExtractor : StreamExtractor<ZipStreamExtractor<ExtractorType, ExtractorOtherType>> {
		ZipStreamExtractor(ExtractorType extractor, ExtractorOtherType other) : left(extractor), right(other) {}

		ExtractorType left;
		ExtractorOtherType right;
		Tuple<std::decay_t<decltype(*left.get())>, std::decay_t<decltype(*right.get())>> value {};

		auto get_impl() {
			value = { *left.get(), *right.get() };
			return &value;
		}

		bool advance_impl() {
			return left.advance() && right.advance();
		}

	};


	// TODO: extractors are copied every time...
	// (n+1)th extractor will copy a chain of n extractors 
	template<typename ExtractorType>
	struct BaseStreamInterface {
		ExtractorType extractor;
		using value_type = std::remove_reference_t<decltype(*extractor.get())>;

		BaseStreamInterface(ExtractorType e) : extractor(e) {}

		// Intermediate Operations

		template<typename Transform>
		auto map(Transform&& transform) {
			using Extractor = MapStreamExtractor<decltype(extractor), Transform>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Transform>(transform)));
		}

		// expects that std::begin and std::end can be called on the result of transform
		template<typename Transform>
		auto flatMap(Transform&& transform) {
			using Extractor = FlatMapStreamExtractor<decltype(extractor), Transform>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Transform>(transform)));
		}

		// add flatten level
		auto flatten() {
			const auto flat = [](auto&& e) { return e; };
			using Extractor = FlatMapStreamExtractor<decltype(extractor), decltype(flat)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::move(flat)));
		}

		template<typename Predicate>
		auto filter(Predicate&& predicate) {
			using Extractor = FilterStreamExtractor<decltype(extractor), Predicate>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Predicate>(predicate)));
		}

		auto skip(size_t count) {
			using Extractor = SkipFirstStreamExtractor<decltype(extractor)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, count));
		}

		template<typename Predicate>
		auto skipWhile(Predicate&& predicate) {
			using Extractor = SkipWhileStreamExtractor<decltype(extractor), Predicate>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Predicate>(predicate)));
		}

		auto take(size_t count) {
			using Extractor = TakeStreamExtractor<decltype(extractor)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, count));
		}

		template<typename Predicate>
		auto takeWhile(Predicate&& predicate) {
			using Extractor = TakeWhileStreamExtractor<decltype(extractor), Predicate>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Predicate>(predicate)));
		}

		template<typename Inspector>
		auto inspect(Inspector&& inspector) {
			using Extractor = InspectStreamExtractor<decltype(extractor), Inspector>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Inspector>(inspector)));
		}

		template<typename Inspector>
		auto spy(Inspector&& inspector) {
			using Extractor = SpyStreamExtractor<decltype(extractor), Inspector>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, std::forward<Inspector>(inspector)));
		}

		auto enumerate(size_t from = 0) {
			using Extractor = EnumerateStreamExtractor<decltype(extractor)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, from));
		}

		auto enumerateTup(size_t from = 0) {
			using Extractor = EnumerateTupleStreamExtractor<decltype(extractor)>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, from));
		}

        template <template<typename> class StreamOther, typename OtherExtractor>
        auto chain(StreamOther<OtherExtractor> other) {
            using Extractor = ChainStreamExtractor<decltype(extractor), OtherExtractor>;
            return BaseStreamInterface<Extractor>(Extractor(extractor, other.extractor));
		}

		template <template<typename> class StreamOther, typename OtherExtractor>
		auto zip(StreamOther<OtherExtractor> other) {
			using Extractor = ZipStreamExtractor<decltype(extractor), OtherExtractor>;
			return BaseStreamInterface<Extractor>(Extractor(extractor, other.extractor));
		}


		// Non-Terminal

		Optional<value_type> next() {
			if (extractor.advance()) {
				return{ *extractor.get() };
			}
			return{};
		}

		Optional<value_type> nth(size_t n) {
			while (n && extractor.advance()) {
				--n;
			}
			return next();
		}
		// Terminal Operations 

		Optional<value_type> last() {
			if (!extractor.advance()) {
				return nullopt;
			} else {
				auto ptr = extractor.get();
				while (extractor.advance()) {
					ptr = extractor.get();
				}				
				return *ptr;			
			}
		}

		template<typename Callable>
		void forEach(Callable&& callable) {
			while (extractor.advance()) {
				callable(*extractor.get());
			}
		}

		size_t count() {
			size_t counter = 0;
			while (extractor.advance()) {
				++counter;
			}
			return counter;
		}

		template<typename Predicate>
		bool any(Predicate&& predicate) {
			while (extractor.advance()) {
				if (predicate(*extractor.get())) {
					return true;
				}
			}
			return false;
		}

		template<typename Predicate>
		bool all(Predicate&& predicate) {
			while (extractor.advance()) {
				if (!predicate(*extractor.get())) {
					return false;
				}
			}
			return true;
		}

		template<typename Accumulator, typename Fold>
		Accumulator fold(Accumulator a, Fold&& fold) {
			while (extractor.advance()) {
				a = fold(a, *extractor.get());
			}
			return a;
		}

        template <template<class...> class Container = std::vector, typename Element = std::remove_const_t<value_type>>
        auto collect() {
            Container<Element> container;
            while (extractor.advance()) {
                container.push_back(*extractor.get());
            }
            return container;
        }

	};

	template<typename Container>
	auto from(const Container& container) {
		using Extractor = SequenceStreamExtractor<decltype(std::begin(container))>;
		return BaseStreamInterface<Extractor>(Extractor(std::begin(container), std::end(container)));
	}

	template<typename Container>
	auto from(const Container&& container) = delete; // currently disastrous

} // namespace streams

#endif // !RUST_STREAMS_H
