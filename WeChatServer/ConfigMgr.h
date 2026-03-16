#pragma once
#include "const.h"

struct SectionInfo {
	SectionInfo(){}
	~SectionInfo() {
		_section_data.clear();
	}

	SectionInfo(const SectionInfo& src) {
		_section_data = src._section_data;
	}

	SectionInfo& operator=(const SectionInfo& src) {
		if (&src == this) {
			return *this;
		}

		this->_section_data = src._section_data;
		return *this;
	}

	std::map<std::string, std::string> _section_data;
	std::string operator[](const std::string& key) {
		auto it = _section_data.find(key);
		if (it == _section_data.end()) {
			return "";
		}
		return it->second;
	}
};


class ConfigMgr
{
public:
	~ConfigMgr() {
		_config_map.clear();
	}
	SectionInfo operator[](const std::string& section) {
		auto it = _config_map;
		if (it.find(section) == it.end()) {
			return SectionInfo();
		}
		return _config_map[section];
	}

	static ConfigMgr& GetInstance() {
		static ConfigMgr cfg_mgr;
		return cfg_mgr;
	}

	/*ConfigMgr(const ConfigMgr& src) {
		_config_map = src._config_map;
	}
	ConfigMgr& operator=(const ConfigMgr& src) {
		if (&src == this) {
			return *this;
		}
		_config_map = src._config_map;
	}*/
	
	ConfigMgr(const ConfigMgr&) = delete;
	ConfigMgr& operator= (const ConfigMgr&) = delete;

private:
	ConfigMgr();
	std::map<std::string, SectionInfo> _config_map;
	
};

