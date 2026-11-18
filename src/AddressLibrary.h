// Minimal reader for Address Library for SKSE Plugins (post-AE ID space).
//   format 2: Skyrim AE 1.6.x  (delta-encoded stream, versionlib-1-6-*.bin)
//   format 5: Skyrim 1.7.x     (dense table of uint32 offsets indexed by ID)
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

class AddressLibrary
{
public:
	// Loads `path` and resolves the requested IDs. Returns false (with `error` set) on any problem.
	bool Resolve(const std::wstring& path, const std::uint32_t* ids, std::size_t count, std::uint64_t* outOffsets)
	{
		for (std::size_t i = 0; i < count; ++i) {
			outOffsets[i] = 0;
		}
		if (!ReadAll(path)) {
			error = "cannot read address library file";
			return false;
		}
		if (size_ < 4) {
			error = "address library file too small";
			return false;
		}
		format = U32(0);
		bool ok = false;
		if (format == 5) {
			ok = ResolveFormat5(ids, count, outOffsets);
		} else if (format == 2) {
			ok = ResolveFormat2(ids, count, outOffsets);
		} else {
			error = "unsupported address library format";
			return false;
		}
		if (!ok) {
			return false;
		}
		for (std::size_t i = 0; i < count; ++i) {
			if (!outOffsets[i]) {
				error = "id not found in address library";
				return false;
			}
		}
		return true;
	}

	std::uint32_t format = 0;
	const char* error = "";

private:
	std::vector<std::uint8_t> data_;
	std::size_t size_ = 0;
	std::size_t pos_ = 0;
	bool overrun_ = false;

	bool ReadAll(const std::wstring& path)
	{
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
			return false;
		}
		std::fseek(f, 0, SEEK_END);
		const long sz = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (sz <= 0) {
			std::fclose(f);
			return false;
		}
		data_.resize(static_cast<std::size_t>(sz));
		const std::size_t got = std::fread(data_.data(), 1, data_.size(), f);
		std::fclose(f);
		size_ = data_.size();
		return got == size_;
	}

	std::uint32_t U32(std::size_t at) const
	{
		std::uint32_t v = 0;
		if (at + 4 <= size_) {
			std::memcpy(&v, data_.data() + at, 4);
		}
		return v;
	}

	template <class T>
	T Take()
	{
		T v{};
		if (pos_ + sizeof(T) > size_) {
			overrun_ = true;
			pos_ = size_;
			return v;
		}
		std::memcpy(&v, data_.data() + pos_, sizeof(T));
		pos_ += sizeof(T);
		return v;
	}

	bool ResolveFormat5(const std::uint32_t* ids, std::size_t count, std::uint64_t* out)
	{
		// header: format, version[4], name[64], pointerSize, reserved, count, then uint32 offsets[count]
		if (size_ < 0x60 || U32(0x54) != 8 || std::memcmp(data_.data() + 0x14, "SkyrimSE.exe", 12) != 0) {
			error = "bad format-5 header";
			return false;
		}
		const std::uint32_t n = U32(0x5C);
		if (size_ < 0x60 + static_cast<std::size_t>(n) * 4) {
			error = "format-5 table truncated";
			return false;
		}
		for (std::size_t i = 0; i < count; ++i) {
			out[i] = ids[i] < n ? U32(0x60 + static_cast<std::size_t>(ids[i]) * 4) : 0;
		}
		return true;
	}

	bool ResolveFormat2(const std::uint32_t* ids, std::size_t count, std::uint64_t* out)
	{
		// header: format, version[4], nameLen, name[nameLen], pointerSize, addressCount, then encoded pairs
		pos_ = 4 + 16;
		const auto nameLen = Take<std::int32_t>();
		if (nameLen < 0 || pos_ + static_cast<std::size_t>(nameLen) > size_) {
			error = "bad format-2 header";
			return false;
		}
		pos_ += static_cast<std::size_t>(nameLen);
		const auto ptrSize = Take<std::int32_t>();
		const auto addrCount = Take<std::int32_t>();
		if (ptrSize != 8 || addrCount <= 0 || overrun_) {
			error = "bad format-2 header";
			return false;
		}
		std::uint64_t prevId = 0, prevOff = 0;
		std::size_t found = 0;
		for (std::int32_t k = 0; k < addrCount && !overrun_; ++k) {
			const auto type = Take<std::uint8_t>();
			const unsigned lo = type & 0xF;
			const unsigned hi = type >> 4;
			std::uint64_t id = 0;
			switch (lo) {
			case 0: id = Take<std::uint64_t>(); break;
			case 1: id = prevId + 1; break;
			case 2: id = prevId + Take<std::uint8_t>(); break;
			case 3: id = prevId - Take<std::uint8_t>(); break;
			case 4: id = prevId + Take<std::uint16_t>(); break;
			case 5: id = prevId - Take<std::uint16_t>(); break;
			case 6: id = Take<std::uint16_t>(); break;
			case 7: id = Take<std::uint32_t>(); break;
			default: error = "bad format-2 id encoding"; return false;
			}
			const std::uint64_t base = (hi & 8) ? prevOff / static_cast<std::uint64_t>(ptrSize) : prevOff;
			std::uint64_t off = 0;
			switch (hi & 7) {
			case 0: off = Take<std::uint64_t>(); break;
			case 1: off = base + 1; break;
			case 2: off = base + Take<std::uint8_t>(); break;
			case 3: off = base - Take<std::uint8_t>(); break;
			case 4: off = base + Take<std::uint16_t>(); break;
			case 5: off = base - Take<std::uint16_t>(); break;
			case 6: off = Take<std::uint16_t>(); break;
			case 7: off = Take<std::uint32_t>(); break;
			}
			if (hi & 8) {
				off *= static_cast<std::uint64_t>(ptrSize);
			}
			for (std::size_t i = 0; i < count; ++i) {
				if (ids[i] == id && !out[i]) {
					out[i] = off;
					++found;
				}
			}
			if (found == count) {
				break;
			}
			prevId = id;
			prevOff = off;
		}
		if (overrun_) {
			error = "format-2 stream truncated";
			return false;
		}
		return true;
	}
};
