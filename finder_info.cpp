

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
#define AFP_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define XATTR_FINDERINFO_NAME "AFP_AfpInfo"
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#if defined(__APPLE__)
#include <sys/xattr.h>
#endif

#if defined(__linux__)
#include <sys/xattr.h>
#define XATTR_FINDERINFO_NAME "user.com.apple.FinderInfo"
#endif

#if defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/extattr.h>
#endif

#if defined(_AIX)
#include <sys/ea.h>
#endif

#include <string>
#include <cstring>
#include <cstdint>

#ifndef XATTR_FINDERINFO_NAME
#define XATTR_FINDERINFO_NAME "com.apple.FinderInfo"
#endif

namespace {

	bool file_type_to_finder_info(uint8_t *buffer, uint16_t file_type, uint32_t aux_type) {
		if (file_type > 0xff || aux_type > 0xffff) return false;

		if (!file_type && aux_type == 0x0000) {
			memcpy(buffer, "BINApdos", 8);
			return true;
		}

		if (file_type == 0x04 && aux_type == 0x0000) {
			memcpy(buffer, "TEXTpdos", 8);
			return true;
		}

		if (file_type == 0xff && aux_type == 0x0000) {
			memcpy(buffer, "PSYSpdos", 8);
			return true;
		}

		if (file_type == 0xb3 && aux_type == 0x0000) {
			memcpy(buffer, "PS16pdos", 8);
			return true;
		}

		if (file_type == 0xd7 && aux_type == 0x0000) {
			memcpy(buffer, "MIDIpdos", 8);
			return true;
		}
		if (file_type == 0xd8 && aux_type == 0x0000) {
			memcpy(buffer, "AIFFpdos", 8);
			return true;
		}
		if (file_type == 0xd8 && aux_type == 0x0001) {
			memcpy(buffer, "AIFCpdos", 8);
			return true;
		}
		if (file_type == 0xe0 && aux_type == 0x0005) {
			memcpy(buffer, "dImgdCpy", 8);
			return true;
		}


		memcpy(buffer, "p   pdos", 8);
		buffer[1] = (file_type) & 0xff;
		buffer[2] = (aux_type >> 8) & 0xff;
		buffer[3] = (aux_type) & 0xff;
		return true;
	}

}

#if defined(AFP_WIN32)
int set_prodos_file_type(const std::string &path, uint16_t fileType, uint32_t auxType) {

	#pragma pack(push, 2)
	struct AFP_Info {
		uint32_t magic;
		uint32_t version;
		uint32_t file_id;
		uint32_t backup_date;
		uint8_t finder_info[32];
		uint16_t prodos_file_type;
		uint32_t prodos_aux_type;
		uint8_t reserved[6];
	};
	#pragma pack(pop)


	HANDLE h;
	BOOL ok;
	AFP_Info info;
	memset(&info, 0, sizeof(info));

	// little endian.
	info.magic = 0x00504641;
	info.version = 0x00010000;
	info.backup_date = 0x80000000;
	info.prodos_file_type = fileType;
	info.prodos_aux_type = auxType;

	file_type_to_finder_info(&info.finder_info, fileType, auxType);

	std::string xpath(path);
	xpath += ":" XATTR_FINDERINFO_NAME;
	h = CreateFileA(xpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return -1;
	ok = WriteFile(h, &info, sizeof(info), nullptr, nullptr);
	CloseHandle(h);
	return ok ? 0 : -1;
}
#elif defined(__sun__)
int set_prodos_file_type(const std::string &path, uint16_t fileType, uint32_t auxType) {

	int pfd;
	int fd;
	int ok;

	uint8_t finder_info[32];
	memset(finder_info, 0, sizeof(finder_info));

	if (!file_type_to_finder_info(&info.finder_info, fileType, auxType))
		return -1;

	pfd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
	if (pfd < 0) return -1;

	fd = opeant(pfd, XATTR_FINDERINFO_NAME, O_WRONLY | O_CREAT | O_TRUNC | O_XATTR, 0666);
	if (fd < 0) {
		close(pfd);
		return -1;
	}

	ok = write(fd, finder_info, sizeof(finder_info));

	close(fd);
	close(pfd);
	return ok == sizeof(finder_info);
}
#else
int set_prodos_file_type(const std::string &path, uint16_t fileType, uint32_t auxType) {

	int ok;

	uint8_t finder_info[32];
	memset(finder_info, 0, sizeof(finder_info));

	if (!file_type_to_finder_info(finder_info, fileType, auxType))
		return -1;

#if defined(__APPLE__)
	ok = setxattr(path.c_str(), XATTR_FINDERINFO_NAME, finder_info, sizeof(finder_info), 0, 0);
#elif defined(__linux__) 
	ok = setxattr(path.c_str(), XATTR_FINDERINFO_NAME, finder_info, sizeof(finder_info), 0);
#elif defined(__FreeBSD__)
	ok = extattr_set_file(path.c_str(), EXTATTR_NAMESPACE_USER, XATTR_FINDERINFO_NAME, finder_info, sizeof(finder_info)) == sizeof(finder_info) ? 0 : -1;
#elif defined(_AIX)
	ok = setea(path.c_str(), XATTR_FINDERINFO_NAME, finder_info, sizeof(finder_info), 0);
#else
	ok = -1;
#endif
	return ok;
}
#endif
