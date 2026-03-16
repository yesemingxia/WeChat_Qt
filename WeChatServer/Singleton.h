#pragma once
#include <iostream>
#include <memory>

template <typename T>
class Singleton {
protected:
	Singleton() = default;
	Singleton(const Singleton&) = delete;
	Singleton& operator =(const Singleton&) = delete;

	static std::shared_ptr<T> _instance;
public:
	static std::shared_ptr<T> GetInstance() {
		static std::once_flag s_flag;
		std::call_once(s_flag, [&]() {
			_instance = std::shared_ptr<T>(new T);
			});

			return _instance;
	}

	~Singleton() {
		std::cout << "this is Singleton destruct" << std::endl;
	}

	void PrintAddress() {
		std::cout << _instance.get() << std::endl;
	}

};

template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;