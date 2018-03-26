#include <memory>
#include <iterator>
#include <atomic>
#include <cassert>

namespace InPlace
{
	template<typename T, size_t N>
	struct Values
	{
		T v[N];

		T* begin()
		{
			return std::begin(v);
		}

		const T* begin() const
		{
			return std::begin(v);
		}

		T* end()
		{
			return std::end(v);
		}

		const T* end() const
		{
			return std::end(v);
		}
	};

	template<typename T>
	struct Values<T, 0>
	{
		T* begin()
		{
			return nullptr;
		}

		const T* begin() const
		{
			return nullptr;
		}

		T* end()
		{
			return nullptr;
		}

		const T* end() const
		{
			return nullptr;
		}
	};

	template<typename T, size_t N>
	struct Array
	{
		Values<T, N> inplace;
		std::unique_ptr<T[]> inheap;

		Array()
		{
		}

		template<size_t M>
		Array(Array<T, M>&& _other) :
			inheap(std::move(_other.inheap))
		{
			if (!inheap)
			{
				set(_other.inplace.begin(), M);
			}
		}

		template<size_t M>
		Array(const T(&_vn)[M])
		{
			set(_vn, M);
		}

		Array(const T* _ptr, size_t _count)
		{
			set(_ptr, _count);
		}

		template<size_t M>
		Array& operator=(Array<T, M>&& _other)
		{
			inheap = std::move(_other.inheap);

			if (!inheap)
			{
				set(_other.inplace.begin(), M);
			}

			return *this;
		}

		template<size_t M>
		Array& operator=(const T(&_vn)[M])
		{
			set(_vn, M);

			return *this;
		}

		operator T*()
		{
			return const_cast<T*>(data());
		}

		operator const T*() const
		{
			return data();
		}

		const T* data() const
		{
			if (!inheap)
			{
				return inplace.begin();
			}
			else
			{
				return inheap.get();
			}
		}

		T* alloc(size_t _size = N)
		{
			if (_size <= N)
			{
				inheap.reset();

				return inplace.begin();
			}
			else
			{
				inheap.reset(new (std::nothrow) T[_size]);

				return inheap.get();
			}
		}

		void set(const T* _ptr, size_t _count)
		{
#ifdef _MSC_VER
			std::copy(_ptr, _ptr + _count, stdext::checked_array_iterator<T*>(alloc(_count), _count));
#else
			std::copy(_ptr, _ptr + _count, alloc(_count));
#endif
		}

	private:
		Array(const Array&);
		Array& operator=(const Array&);
	};

	struct VObject
	{
		virtual void release_to_factory() = 0;

		struct default_deleter
		{
			void operator ()(VObject * _obj)
			{
				if (_obj)
				{
					_obj->release_to_factory();
				}
			}
		};
	};

	template<typename T>
	struct Object : public VObject, public T
	{
		struct tag {
			template<typename... TN>
			static void init_from_factory(TN&&... _vn)
			{
			}

			static void clear_from_factory()
			{
			}
		};

		template<typename... TN>
		Object(tag, TN&&... _vn) :
			T(std::forward<TN&&>(_vn)...)
		{
		}

		template<typename... TN>
		Object(TN&&... _vn) :
			T(std::forward<TN&&>(_vn)...)
		{
		}

		template<typename Q>
		Object& operator=(Q&& _v)
		{
			T::operator=(std::forward<Q&&>(_v));

			return *this;
		}

		template<typename Q>
		struct has_it
		{
		private:
			template<typename C> static char test(decltype(&C::clear_from_factory));
			template<typename C> static int  test(...);
		public:
			enum { value = sizeof(test<Q>(0)) == sizeof(char) };
		};

		//
		// init_from_factory
		//
		template<typename Q = T, typename... TN>
		typename std::enable_if<has_it<Q>::value, void>::type init_from_factory(TN&&... _vn)
		{
			Q::init_from_factory(std::forward<TN&&>(_vn)...);
		}

		template<typename Q = T, typename... TN>
		typename std::enable_if<!has_it<Q>::value, void>::type init_from_factory(TN&&... _vn)
		{
			Q::operator=(std::move(Q(std::forward<TN&&>(_vn)...)));
		}

		//
		// clear_from_factory
		//
		template<typename Q = T>
		typename std::enable_if<has_it<Q>::value, void>::type clear_from_factory()
		{
			Q::clear_from_factory();
		}

		template<typename Q = T>
		typename std::enable_if<!has_it<Q>::value, void>::type clear_from_factory()
		{
			Q::operator=(std::move(Q()));
		}
	};

	template<typename T, typename Base = Object<T>>
	struct ObjectInHeap : public Base
	{
		static_assert(std::is_base_of<InPlace::VObject, Base>::value, "Base must inherit from VObject!");

		template<typename... TN>
		ObjectInHeap(TN&&... _vn) :
			Base(std::forward<TN&&>(_vn)...)
		{
		}

		virtual ~ObjectInHeap()
		{
		}

		template<typename Q>
		ObjectInHeap& operator=(Q&& _v)
		{
			T::operator=(std::forward<Q&&>(_v));

			return *this;
		}

		virtual void release_to_factory()
		{
			delete this;
		}
	};

	template<typename T, typename Base = Object<T>>
	struct ObjectInPlace : public Base, public std::atomic_flag
	{
		static_assert(std::is_base_of<InPlace::VObject, Base>::value, "Base must inherit from VObject!");

		template<typename... TN>
		ObjectInPlace(TN&&... _vn) :
			Base(typename Object<T>::tag(), std::forward<TN&&>(_vn)...)
		{
			std::atomic_flag::clear();
		}

		ObjectInPlace(Object<T>&& _other) :
			Base(std::forward<Base&&>(_other))
		{
			_other.release_to_factory();
			std::atomic_flag::clear();
		}

		virtual ~ObjectInPlace()
		{
			assert(!std::atomic_flag::test_and_set()); // object (factory?)! being released and this object is still being referenced!
		}

		template<typename Q>
		ObjectInPlace& operator=(Q&& _v)
		{
			T::operator=(std::forward<Q&&>(_v));

			return *this;
		}

		virtual void release_to_factory()
		{
			assert(std::atomic_flag::test_and_set()); // object being relased but it is not being referenced!

			Base::clear_from_factory();

			std::atomic_flag::clear();
		}
	};

	template<typename T, size_t N = 1, typename Base = Object<T>>
	class ObjectFactory
	{
		Values<ObjectInPlace<T, Base>, N> m_values;

	public:
		template<typename... TN>
		Base* alloc(TN&&... _vn)
		{
			for (auto it = m_values.begin(); it != m_values.end(); ++it)
			{
				if (!it->test_and_set())
				{
					it->init_from_factory(std::forward<TN&&>(_vn)...);

					return it;
				}
			}

			return new (std::nothrow) ObjectInHeap<T, Base>(std::forward<TN&&>(_vn)...);
		}

		template<typename... TN>
		std::unique_ptr<Base, typename Base::default_deleter> alloc_autoptr(TN&&... _vn)
		{
			return std::unique_ptr<Base, typename Base::default_deleter>(alloc(std::forward<TN&&>(_vn)...));
		}

		template<typename... TN>
		static Base* static_alloc(TN&&... _vn)
		{
			static ObjectFactory static_value;

			return static_value.alloc(std::forward<TN&&>(_vn)...);
		}

		template<typename... TN>
		static Base* static_alloc_autoptr(TN&&... _vn)
		{
			return std::unique_ptr<Base, typename Base::default_deleter>(static_alloc(std::forward<TN&&>(_vn)...));
		}
	};
}