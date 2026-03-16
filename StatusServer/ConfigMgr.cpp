#include "ConfigMgr.h"
#include <filesystem>

ConfigMgr::ConfigMgr() {
	try{
		std::filesystem::path current_path = std::filesystem::current_path();
		std::filesystem::path config_path = current_path / "config.ini";
		std::cout << "Config path:" << config_path << std::endl;

		if (!std::filesystem::exists(config_path)) {
			throw std::runtime_error("Config file not found: " + config_path.string());
		}
		if (!std::filesystem::is_regular_file(config_path)) {
			throw std::runtime_error("Invalid config file: " + config_path.string() + " (not a regular file)");
		}

		boost::property_tree::ptree pt;
		boost::property_tree::read_ini(config_path.string(), pt);

		for (const auto& section_pair : pt) {
			const ::std::string& section_name = section_pair.first;
			const boost::property_tree::ptree& section_tree = section_pair.second;
			std::map<std::string, std::string> section_config;
			for (const auto& key_value_pair : section_tree) {
				const std::string& key = key_value_pair.first;
				const std::string& value = key_value_pair.second.get_value<std::string>();
				if (value.empty()) {
					std::cerr << "Warning: [" << section_name << "] " << key << " is empty!" << std::endl;
				}
				section_config[key] = value;
			}

			SectionInfo sectionInfo;
			sectionInfo._section_data = section_config;
			_config_map[section_name] = sectionInfo;
		}

		for (const auto& section_entry : _config_map) {
			const std::string& section_name = section_entry.first;
			SectionInfo section_config = section_entry.second;
			std::cout << "[" << section_name << "]" << std::endl;
			for (const auto& key_value_pair : section_config._section_data) {
				std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
			}

		}
	}
	catch (const boost::property_tree::ini_parser::ini_parser_error& e) {
		// 捕获INI解析异常（核心：解决你之前的崩溃问题）
		std::cerr << "\nError: INI parser failed!" << std::endl;
		std::cerr << "  File: " << e.filename() << std::endl;
		std::cerr << "  Line: " << e.line() << std::endl;
		std::cerr << "  Reason: " << e.what() << std::endl;
		throw; // 可选：重新抛出异常，让上层处理；也可直接return终止构造
	}
	catch (const std::filesystem::filesystem_error& e) {
		// 捕获文件系统异常（如路径访问失败）
		std::cerr << "\nError: Filesystem error - " << e.what() << std::endl;
		throw;
	}
	catch (const std::runtime_error& e) {
		// 捕获自定义的运行时异常（文件不存在/非法）
		std::cerr << "\nError: Runtime error - " << e.what() << std::endl;
		throw;
	}
	catch (const std::exception& e) {
		// 捕获所有其他标准异常
		std::cerr << "\nError: Unknown error - " << e.what() << std::endl;
		throw;
	}
}
