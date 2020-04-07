#include <string>

class okdos
{
public:
	okdos();
	~okdos();
	bool m_okdos_ready;
	bool do_extract(const std::string& t7zPath, const std::string& strima, const std::string& v_extract_to);
	std::string make_bat_string();
private:
	std::string m_t7zPath;
	std::string m_ima;
	std::string m_extract_to;
	bool m_need_del_folder;
};