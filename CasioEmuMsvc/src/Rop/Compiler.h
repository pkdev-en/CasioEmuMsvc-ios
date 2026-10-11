#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace lc::details {
	using namespace std;

	// ==========================
	// Utility
	// ==========================

	static string ltrim(string s) {
		s.erase(s.begin(), find_if(s.begin(), s.end(),
							   [](unsigned char c) { return !isspace(c); }));
		return s;
	}

	static string rtrim(string s) {
		s.erase(find_if(s.rbegin(), s.rend(),
					[](unsigned char c) { return !isspace(c); })
					.base(),
			s.end());
		return s;
	}

	static string trim(string s) {
		return rtrim(ltrim(std::move(s)));
	}

	static string lower(string s) {
		transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return char(tolower(c)); });
		return s;
	}

	static bool startsWith(const string& s, const string& p) {
		return s.rfind(p, 0) == 0;
	}

	static bool endsWith(const string& s, const string& p) {
		return s.size() >= p.size() &&
			   equal(p.rbegin(), p.rend(), s.rbegin());
	}

	static vector<string> split(const string& s, char ch) {
		vector<string> r;
		string cur;
		stringstream ss(s);
		while (getline(ss, cur, ch))
			r.push_back(cur);
		return r;
	}

	static string removeInlineComment(string line) {
		size_t p = line.find('#');
		if (p != string::npos)
			line.resize(p);
		return rtrim(line);
	}

	// 模拟 Python 的 canonicalize，剥离非字母/数字前后的空格（同时安全跳过字符串内容）
	static string canonicalize(string st) {
		st = trim(std::move(st));
		string out;
		bool in_quote = false;
		for (size_t i = 0; i < st.size(); ++i) {
			if (st[i] == '"') {
				in_quote = !in_quote;
				out.push_back('"');
			}
			else if (!in_quote && st[i] == ' ') {
				bool left_alnum = (out.size() > 0) && isalnum((unsigned char)out.back());
				size_t j = i + 1;
				while (j < st.size() && st[j] == ' ')
					++j;
				bool right_alnum = (j < st.size()) && isalnum((unsigned char)st[j]);

				if (left_alnum && right_alnum) {
					out.push_back(' ');
				}
				i = j - 1; // skip extra spaces
			}
			else {
				out.push_back(st[i]);
			}
		}
		return out;
	}

	static long long parseInteger(string s, int base = 0) {
		s = trim(s);
		size_t idx = 0;
		long long v = stoll(s, &idx, base);
		if (idx != s.size()) {
			throw runtime_error("invalid integer: " + s);
		}
		return v;
	}

	static string hexWord(int v, int width = 4) {
		stringstream ss;
		ss << "0x" << hex << nouppercase << setw(width)
		   << setfill('0') << v;
		return ss.str();
	}

	static string byteToKey(int byte, const vector<int>& npress, const vector<string>& symbolrepr) {
		if (byte == 0)
			return "<NUL>";

		string sym = symbolrepr.empty() ? "" : symbolrepr[byte];
		if (sym == "@" || sym.empty()) {
			char buf[16];
			snprintf(buf, sizeof(buf), "<%02x>", byte);
			return buf;
		}

		int offset = 0;
		int current_byte = byte;
		while (current_byte > 0 && npress[current_byte] >= 100) {
			current_byte--;
			offset++;
		}

		string typesym = current_byte > 0 ? symbolrepr[current_byte] : "NUL";

		if (offset == 0) {
			return sym;
		}
		else {
			return "<" + sym + ":" + typesym + "+" + to_string(offset) + ">";
		}
	}

	// ==========================
	// Diagnostics
	// ==========================

	struct Diagnostic {
		string file;
		int line = 0;

		[[noreturn]] void error(const string& msg) const {
			stringstream ss;
			if (!file.empty())
				ss << file << ":";
			if (line > 0)
				ss << line << ": ";
			ss << msg;
			throw runtime_error(ss.str());
		}

		void warn(const string& msg) const {
			if (!file.empty())
				cerr << file << ":";
			if (line > 0)
				cerr << line << ": ";
			cerr << "warning: " << msg << "\n";
		}
	};

	// ==========================
	// Lexer
	// ==========================

	enum class TokenKind {
		Identifier,
		Number,
		String,
		Symbol,
		End
	};

	struct Token {
		TokenKind kind = TokenKind::End;
		string text;
		size_t pos = 0;
	};

	class Lexer {
		string src;
		size_t pos = 0;

	public:
		explicit Lexer(string s) : src(std::move(s)) {}

		Token next() {
			skipSpace();
			if (pos >= src.size())
				return {TokenKind::End, "", pos};

			size_t start = pos;
			char c = src[pos];

			if (isalpha((unsigned char)c) || c == '_' || c == '.' || c == '[' || c == ']') {
				string out;
				while (pos < src.size()) {
					char x = src[pos];
					if (isalnum((unsigned char)x) ||
						x == '_' || x == '.' || x == '[' || x == ']' ||
						x == ',' || x == '-' || x == '+' || x == '>') {
						out.push_back(x);
						++pos;
					}
					else {
						break;
					}
				}
				return {TokenKind::Identifier, out, start};
			}

			if (isdigit((unsigned char)c)) {
				string out;
				while (pos < src.size()) {
					char x = src[pos];
					if (isxdigit((unsigned char)x) || x == 'x' || x == 'X') {
						out.push_back(x);
						++pos;
					}
					else {
						break;
					}
				}
				return {TokenKind::Number, out, start};
			}

			if (c == '"') {
				++pos;
				string out;
				while (pos < src.size() && src[pos] != '"') {
					if (src[pos] == '\\' && pos + 1 < src.size()) {
						char e = src[++pos];
						switch (e) {
						case 'n':
							out.push_back('\n');
							break;
						case 't':
							out.push_back('\t');
							break;
						case '"':
							out.push_back('"');
							break;
						case '\\':
							out.push_back('\\');
							break;
						default:
							out.push_back(e);
							break;
						}
						++pos;
					}
					else {
						out.push_back(src[pos++]);
					}
				}
				if (pos < src.size() && src[pos] == '"')
					++pos;
				return {TokenKind::String, out, start};
			}

			++pos;
			return {TokenKind::Symbol, string(1, c), start};
		}

	private:
		void skipSpace() {
			while (pos < src.size() && isspace((unsigned char)src[pos]))
				++pos;
		}
	};

	// ==========================
	// Command DB & AST
	// ==========================

	struct BuiltinCommand {
		int address = 0;
		vector<string> tags;
	};

	class CommandDatabase {
		unordered_map<string, BuiltinCommand> commands;
		unordered_map<string, int> dataLabels;

	public:
		void addCommand(int address, string name, vector<string> tags = {}) {
			name = lower(canonicalize(name)); // 确保存入的 Key 被正确格式化
			if (name.empty())
				throw runtime_error("empty command");

			for (string p : {"0x", "call", "goto", "adr_of"}) {
				if (startsWith(name, p))
					throw runtime_error("illegal command prefix: " + name);
			}

			for (auto& [old, cmd] : commands) {
				if (old == name || cmd.address == address) {
					throw runtime_error("duplicated command: " + name);
				}
			}
			commands[name] = BuiltinCommand{address, std::move(tags)};
		}

		bool hasCommand(const string& name) const { return commands.count(lower(name)); }

		const BuiltinCommand& getCommand(const string& name) const {
			auto it = commands.find(lower(name));
			if (it == commands.end())
				throw runtime_error("unknown command: " + name);
			return it->second;
		}

		void addDataLabel(string name, int address) { dataLabels[lower(canonicalize(name))] = address; }
		bool hasDataLabel(const string& name) const { return dataLabels.count(lower(name)); }

		int getDataLabel(const string& name) const {
			auto it = dataLabels.find(lower(name));
			if (it == dataLabels.end())
				throw runtime_error("unknown data label: " + name);
			return it->second;
		}
	};

	struct EmptyStmt {};
	struct LabelStmt {
		string name;
	};
	struct HexLiteralStmt {
		string text;
	};
	struct RawHexStmt {
		string hex;
	};
	struct CallStmt {
		string target;
	};
	struct GotoStmt {
		string label;
	};
	struct AdrOfStmt {
		int offset = 0;
		string label;
	};
	struct AssignmentStmt {
		string reg;
		string valueText;
	};
	struct OrgStmt {
		string expr;
	};
	struct BackupIsStmt {
		string value;
	};
	struct AddrCopyIsStmt {
		string value;
	};
	struct MacroStmt {
		string name;
	};
	struct AdrArithStmt {
		int leftOffset = 0;
		string leftLabel;
		int rightOffset = 0;
		string rightLabel;
	};
	struct PrLengthStmt {};
	struct RemainingLengthStmt {};
	struct LBytesStmt {};
	struct StrStoreStmt {
		string name;
		string text;
	};
	struct StrEmitStmt {
		string text;
	};
	struct StrUseStmt {
		string name;
	};
	struct FreeformStmt {
		string text;
	};

	using Stmt = variant<
		EmptyStmt, LabelStmt, HexLiteralStmt, RawHexStmt, CallStmt, GotoStmt,
		AdrOfStmt, AssignmentStmt, OrgStmt, BackupIsStmt, AddrCopyIsStmt,
		MacroStmt, AdrArithStmt, PrLengthStmt, RemainingLengthStmt,
		LBytesStmt, StrStoreStmt, StrEmitStmt, StrUseStmt, FreeformStmt>;

	// ==========================
	// Parser
	// ==========================

	class Parser {
		Diagnostic diag;
		const CommandDatabase* db; // 添加 db 指针感知内置小挂件

	public:
		explicit Parser(const CommandDatabase* db = nullptr, Diagnostic d = {}) : diag(std::move(d)), db(db) {}

		Stmt parseLine(string line) {
			line = removeInlineComment(std::move(line));
			line = trim(line);

			if (line.empty())
				return EmptyStmt{};

			string low = lower(line);

			// 如果已经存在于 Commands 中，则直接作为 Call 执行。
			// 这是为了防止 `[er0]=er2,rt` 被误判为 Assignment 赋值从而崩溃。
			if (db && db->hasCommand(low)) {
				return CallStmt{low};
			}

			if (endsWith(line, ":")) {
				return LabelStmt{lower(trim(line.substr(0, line.size() - 1)))};
			}

			if (startsWith(low, "0x"))
				return HexLiteralStmt{low};
			if (startsWith(low, "hex") && low.find("hex_") == string::npos)
				return RawHexStmt{trim(line.substr(3))};
			if (startsWith(low, "call"))
				return CallStmt{lower(trim(line.substr(4)))};
			if (startsWith(low, "goto"))
				return GotoStmt{lower(trim(line.substr(4)))};
			if (startsWith(low, "adr_of"))
				return parseAdrOf(trim(low.substr(6)));
			if (startsWith(low, "org"))
				return OrgStmt{trim(line.substr(3))};
			if (startsWith(low, "backup is "))
				return BackupIsStmt{trim(line.substr(10))};
			if (startsWith(low, "addrcopy is "))
				return AddrCopyIsStmt{trim(line.substr(12))};

			if (startsWith(low, "loop880") || startsWith(low, "loop580") ||
				startsWith(low, "backup580") || startsWith(low, "backup880")) {
				return MacroStmt{low};
			}

			if (startsWith(low, "adr_arith"))
				return parseAdrArith(low);
			if (startsWith(low, "pr_length"))
				return PrLengthStmt{};
			if (startsWith(low, "remaining_length"))
				return RemainingLengthStmt{};
			if (startsWith(low, "l_bytes"))
				return LBytesStmt{};

			if (startsWith(low, "str")) {
				return parseStringStmt(line);
			}

			size_t eq = line.find('=');
			if (eq != string::npos) {
				string reg = lower(trim(line.substr(0, eq)));
				string value = trim(line.substr(eq + 1));
				return AssignmentStmt{reg, lower(value)};
			}

			return FreeformStmt{low};
		}

	private:
		AdrOfStmt parseAdrOf(const string& body) {
			if (body.empty())
				diag.error("adr_of requires label");

			int offset = 0;
			string label;

			if (body[0] == '[') {
				size_t rb = body.find(']');
				if (rb == string::npos)
					diag.error("adr_of has unmatched '['");

				string inside = trim(body.substr(1, rb - 1));
				label = trim(body.substr(rb + 1));

				size_t arrow = inside.find("->");
				if (arrow != string::npos) {
					int a = stoi(trim(inside.substr(0, arrow)), nullptr, 16);
					int b = stoi(trim(inside.substr(arrow + 2)), nullptr, 16);
					offset = b > a ? a - b : b - a;
				}
				else {
					offset = int(parseInteger(inside, 0));
				}
			}
			else {
				label = trim(body);
			}

			if (label.empty())
				diag.error("adr_of requires label");
			return AdrOfStmt{offset, label};
		}

		static pair<int, string> parseAdrPart(string part) {
			part = trim(part);
			if (startsWith(part, "adr_arith"))
				part = trim(part.substr(9));

			int offset = 0;
			string label;
			size_t lb = part.find('[');
			size_t rb = part.find(']');

			if (lb != string::npos && rb != string::npos && rb > lb) {
				string off = trim(part.substr(lb + 1, rb - lb - 1));
				offset = int(parseInteger(off, 0));
				label = trim(part.substr(rb + 1));
			}
			else {
				label = trim(part);
			}

			if (label.empty())
				throw runtime_error("adr_arith part missing label");
			return {offset, label};
		}

		AdrArithStmt parseAdrArith(const string& line) {
			int lastMinus = int(line.size()) - 1;
			while (lastMinus > 0) {
				if (line[lastMinus] == '-' && line.find("adr_arith", lastMinus) != string::npos) {
					break;
				}
				--lastMinus;
			}

			if (lastMinus <= 0)
				diag.error("wrong adr_arith syntax");

			string left = trim(line.substr(9, lastMinus - 9));
			string right = trim(line.substr(lastMinus + 1));

			auto [lo, ll] = parseAdrPart(left);
			auto [ro, rl] = parseAdrPart(right);

			return AdrArithStmt{lo, ll, ro, rl};
		}

		Stmt parseStringStmt(const string& line) {
			string content = trim(line.substr(3));
			size_t q = content.find('"');
			if (q != string::npos) {
				string name = trim(content.substr(0, q));
				Lexer lx(content.substr(q));
				Token t = lx.next();

				if (t.kind != TokenKind::String)
					diag.error("invalid str string literal");

				if (name.empty())
					return StrEmitStmt{t.text};
				else
					return StrStoreStmt{name, t.text};
			}

			if (!content.empty())
				return StrUseStmt{content};
			diag.error("wrong str syntax");
		}
	};

	// ==========================
	// Compiler State
	// ==========================

	struct AdrOfFixup {
		int pos;
		int offset;
		string label;
	};
	struct AdrArithFixup {
		int pos;
		int leftOffset;
		string leftLabel;
		int rightOffset;
		string rightLabel;
	};

	struct CompilerState {
		vector<int> result;
		unordered_map<string, int> labels;

		vector<AdrOfFixup> adrOfFixups;
		vector<AdrArithFixup> adrArithFixups;
		vector<int> prLengthFixups;
		vector<int> remainingFixups;

		optional<int> home;
		optional<int> orgAddress;

		string backup;
		string addrCopy;
		unordered_map<string, string> stringVars;

		int previousSectionLength = 0;
		int remainingBase = 0;

		void resetForSection(int prevLen) {
			result.clear();
			labels.clear();
			adrOfFixups.clear();
			adrArithFixups.clear();
			prLengthFixups.clear();
			remainingFixups.clear();
			home.reset();
			orgAddress.reset();
			backup.clear();
			addrCopy.clear();
			stringVars.clear();
			previousSectionLength = prevLen;
			remainingBase = 0;
		}
	};

	// ==========================
	// Compiler
	// ==========================

	class Compiler {
		CommandDatabase& db;
		CompilerState st;
		Parser parser;

		int maxCallAdr = 0xfffff;
		vector<int> npress;
		unordered_map<unsigned char, vector<int>> charMap;

	public:
		explicit Compiler(CommandDatabase& db, vector<int> npressWeights = vector<int>(256, 1))
			: db(db), parser(&db), npress(std::move(npressWeights)) {
			for (int i = 0; i < 256; ++i)
				charMap[(unsigned char)i] = {i};
		}

		CompilerState& state() { return st; }
		void setPreviousSectionLength(int n) { st.previousSectionLength = n; }
		void setCharMap(unordered_map<unsigned char, vector<int>> m) { charMap = std::move(m); }

		void compileStatement(const Stmt& stmt) {
			std::visit([this](auto&& x) { emit(x); }, stmt);
		}

		void compileLine(const string& line) {
			Stmt stmt = parser.parseLine(line);
			compileStatement(stmt);
		}

		void compileLines(const vector<string>& lines) {
			bool blockComment = false;
			for (string line : lines) {
				string t = trim(line);
				if (startsWith(t, "/*")) {
					blockComment = true;
					continue;
				}
				if (t.find("*/") != string::npos) {
					blockComment = false;
					continue;
				}
				if (blockComment)
					continue;

				string noComment = removeInlineComment(line);
				if (trim(noComment).empty())
					continue;

				string canon = canonicalize(noComment);

				if (canon.find(';') != string::npos) {
					for (auto& part : split(canon, ';'))
						compileLine(part);
				}
				else {
					compileLine(canon);
				}
			}
		}

		void finish() {
			resolveAdrArith();
			resolvePrLength();
			resolveRemaining();
		}

		void resolveAdrOf(int home) {
			for (auto& f : st.adrOfFixups) {
				auto it = st.labels.find(f.label);
				if (it == st.labels.end())
					throw runtime_error("unknown label in adr_of: " + f.label);

				int target = home + it->second + f.offset;
				if (f.pos + 1 >= int(st.result.size()))
					throw runtime_error("adr_of fixup out of range");
				if (st.result[f.pos] != 0 || st.result[f.pos + 1] != 0)
					throw runtime_error("adr_of target already patched");

				st.result[f.pos] = target & 0xff;
				st.result[f.pos + 1] = (target >> 8) & 0xff;
			}
		}

	private:
		void emit(const EmptyStmt&) {}

		void emit(const LabelStmt& x) {
			if (st.labels.count(x.name))
				throw runtime_error("duplicated label: " + x.name);
			st.labels[x.name] = int(st.result.size());
		}

		void emit(const HexLiteralStmt& x) { emitHexLiteral(x.text); }

		void emit(const RawHexStmt& x) {
			string s = x.hex;
			s.erase(remove_if(s.begin(), s.end(), ::isspace), s.end());
			if (s.size() % 2)
				throw runtime_error("invalid hex byte length");

			for (size_t i = 0; i < s.size(); i += 2) {
				int b = stoi(s.substr(i, 2), nullptr, 16);
				st.result.push_back(b);
			}
		}

		void emit(const CallStmt& x) {
			int adr = 0;
			try {
				adr = stoi(x.target, nullptr, 16);
			}
			catch (...) {
				const auto& cmd = db.getCommand(x.target);
				adr = cmd.address;
				for (auto& tag : cmd.tags) {
					if (startsWith(tag, "warning"))
						cerr << tag << "\n";
				}
			}
			if (adr < 0 || adr > maxCallAdr)
				throw runtime_error("invalid call address");
			adr = optimizeAdrForNpress(adr);
			emitHexLiteral(hexWord(adr + 0x30300000, 8));
		}

		void emit(const GotoStmt& x) {
			compileLine("er6 = adr_of [-2] " + x.label);
			compileLine("call sp=er6,pop er8");
		}

		void emit(const AdrOfStmt& x) {
			st.adrOfFixups.push_back(AdrOfFixup{int(st.result.size()), x.offset, x.label});
			st.result.push_back(0);
			st.result.push_back(0);
		}

		void emit(const AssignmentStmt& x) {
			compileLine("call pop " + x.reg);
			size_t old = st.result.size();

			string value = x.valueText;
			replace(value.begin(), value.end(), ',', ';');

			if (value.find(';') != string::npos) {
				for (auto& p : split(value, ';'))
					compileLine(p);
			}
			else {
				compileLine(value);
			}

			int got = int(st.result.size() - old);
			int need = sizeofRegister(x.reg);
			if (got != need) {
				throw runtime_error("register assignment size mismatch: " + x.reg + ", need " + to_string(need) + ", got " + to_string(got));
			}
		}

		void emit(const OrgStmt& x) {
			int hx = int(parseInteger(x.expr, 0));
			st.orgAddress = hx;
			int home = hx - int(st.result.size());
			if (st.home.has_value() && st.home.value() != home)
				throw runtime_error("inconsistent org/home");
			st.home = home;
		}

		void emit(const BackupIsStmt& x) { st.backup = x.value; }
		void emit(const AddrCopyIsStmt& x) { st.addrCopy = x.value; }
		void emit(const MacroStmt& x) { expandMacro(x.name); }

		void emit(const AdrArithStmt& x) {
			st.adrArithFixups.push_back(AdrArithFixup{int(st.result.size()), x.leftOffset, x.leftLabel, x.rightOffset, x.rightLabel});
			st.result.push_back(0);
		}

		void emit(const PrLengthStmt&) {
			st.prLengthFixups.push_back(int(st.result.size()));
			st.result.push_back(0);
			st.result.push_back(0);
		}

		void emit(const RemainingLengthStmt&) {
			st.remainingBase = int(st.result.size());
			st.remainingFixups.push_back(int(st.result.size()));
			st.result.push_back(0);
			st.result.push_back(0);
		}

		void emit(const LBytesStmt&) {
			int n = st.previousSectionLength;
			st.result.push_back(n & 0xff);
			st.result.push_back((n >> 8) & 0xff);
		}

		void emit(const StrStoreStmt& x) { st.stringVars[x.name] = x.text; }

		void emit(const StrEmitStmt& x) {
			auto bytes = encodeString(x.text);
			st.result.insert(st.result.end(), bytes.begin(), bytes.end());
		}

		void emit(const StrUseStmt& x) {
			auto it = st.stringVars.find(x.name);
			if (it == st.stringVars.end())
				throw runtime_error("unknown string variable: " + x.name);
			auto bytes = encodeString(it->second);
			st.result.insert(st.result.end(), bytes.begin(), bytes.end());
		}

		void emit(const FreeformStmt& x) {
			string line = lower(trim(x.text));
			if (db.hasDataLabel(line)) {
				emitHexLiteral(hexWord(db.getDataLabel(line), 4));
				return;
			}

			size_t plus = line.find('+');
			if (plus != string::npos) {
				string name = trim(line.substr(0, plus));
				string off = trim(line.substr(plus + 1));
				if (db.hasDataLabel(name)) {
					int adr = db.getDataLabel(name) + int(parseInteger(off, 0));
					emitHexLiteral(hexWord(adr, 4));
					return;
				}
			}

			if (db.hasCommand(line)) {
				emit(CallStmt{line});
				return;
			}
			throw runtime_error("wrong syntax or unknown command: " + line);
		}

		int getNpress(int b) const { return npress[b & 0xff]; }
		int getNpressAdr(int adr) const { return getNpress(adr & 0xff) + getNpress((adr >> 8) & 0xff); }
		int optimizeAdrForNpress(int adr) const {
			int alt = adr ^ 1;
			return getNpressAdr(alt) < getNpressAdr(adr) ? alt : adr;
		}

		int sizeofRegister(const string& r) const {
			if (r.empty())
				throw runtime_error("empty register");
			switch (r[0]) {
			case 'r':
				return 1;
			case 'e':
				return 2;
			case 'x':
				return 4;
			case 'q':
				return 8;
			default:
				throw runtime_error("Unknown register size or invalid assignment target (must be r,e,x,q): " + r);
			}
		}

		void emitHexLiteral(string line) {
			line = lower(trim(line));
			if (line.find('+') != string::npos) {
				auto p = split(line, '+');
				if (p.size() != 2)
					throw runtime_error("bad hex + syntax");
				long long v = parseInteger(p[0], 16) + parseInteger(p[1], 10);
				int n = int(p[0].size()) / 2 - 1;
				for (int i = 0; i < n; ++i) {
					st.result.push_back(v & 0xff);
					v >>= 8;
				}
				return;
			}

			size_t minus = line.find('-', 2);
			if (minus != string::npos) {
				string a = line.substr(0, minus);
				string b = line.substr(minus + 1);
				long long v = parseInteger(a, 16) - parseInteger(b, 10);
				int n = int(a.size()) / 2 - 1;
				for (int i = 0; i < n; ++i) {
					st.result.push_back(v & 0xff);
					v >>= 8;
				}
				return;
			}

			if (line.size() % 2)
				throw runtime_error("invalid hex literal length");
			long long v = parseInteger(line, 16);
			int n = int(line.size()) / 2 - 1;
			for (int i = 0; i < n; ++i) {
				st.result.push_back(v & 0xff);
				v >>= 8;
			}
		}

		vector<int> encodeString(const string& s) {
			vector<int> out;
			for (unsigned char c : s) {
				auto it = charMap.find(c);
				if (it == charMap.end())
					throw runtime_error("character not in conversion table");
				for (int b : it->second)
					out.push_back(b & 0xff);
			}
			return out;
		}

		void expandMacro(const string& name) {
			vector<string> lines;
			if (startsWith(name, "loop880")) {
				lines = {"set_segment:", "setlr", "di,rt", "call pop xr0", "adr_of length", "0x0001",
					"[er0]=er2,rt", "loop:", "call pop qr0", "adr_of " + st.addrCopy, st.backup,
					"pr_length", "adr_of [-2] " + st.addrCopy, "hex e6 4d", "length:",
					"remaining_length", "0x0000", "call sp=er6,pop er8"};
			}
			else if (startsWith(name, "loop580")) {
				lines = {"set_segment:", "setlr", "di,rt", "call pop xr0", "adr_of length", "0x0001",
					"[er0]=er2,rt", "call pop qr0", "pr_length", st.backup, "adr_of " + st.addrCopy,
					"adr_of [-2] " + st.addrCopy, "0x8932", "length:", "remaining_length", "0x0000", "sp=er6,pop er8"};
			}
			else if (startsWith(name, "backup580")) {
				lines = {"backup:", "call pop xr0", st.backup, "adr_of " + st.addrCopy, "call 0x09450", "pr_length"};
			}
			else if (startsWith(name, "backup880")) {
				lines = {"backup:", "call pop xr0", st.backup, "adr_of " + st.addrCopy, "call 0x14de8", "pr_length", "0x0000"};
			}
			else {
				throw runtime_error("unknown macro: " + name);
			}
			compileLines(lines);
		}

		void resolveAdrArith() {
			for (auto& f : st.adrArithFixups) {
				if (!st.labels.count(f.leftLabel))
					throw runtime_error("unknown label: " + f.leftLabel);
				if (!st.labels.count(f.rightLabel))
					throw runtime_error("unknown label: " + f.rightLabel);
				int l = st.labels[f.leftLabel] + f.leftOffset;
				int r = st.labels[f.rightLabel] + f.rightOffset;
				st.result[f.pos] = (l - r) & 0xff;
			}
		}

		void resolvePrLength() {
			int len = int(st.result.size());
			for (int pos : st.prLengthFixups) {
				st.result[pos] = len & 0xff;
				st.result[pos + 1] = (len >> 8) & 0xff;
			}
		}

		void resolveRemaining() {
			int len = int(st.result.size());
			for (int pos : st.remainingFixups) {
				int rem = len - st.remainingBase;
				st.result[pos] = rem & 0xff;
				st.result[pos + 1] = (rem >> 8) & 0xff;
			}
		}
	};

	// ==========================
	// Source preprocessing
	// ==========================

	class SourcePreprocessor {
	public:
		static vector<vector<string>> splitSections(const vector<string>& src) {
			vector<vector<string>> sections;
			vector<string> cur;

			for (auto line : src) {
				if (trim(line) == "!===") {
					if (!cur.empty()) {
						sections.push_back(cur);
						cur.clear();
					}
				}
				else {
					cur.push_back(line);
				}
			}
			if (!cur.empty())
				sections.push_back(cur);
			if (sections.empty())
				sections.push_back(src);
			return sections;
		}

		static vector<string> processSetupLoop(const vector<string>& program) {
			vector<string> modified;
			bool setup_loop_detected = false;

			for (string line : program) {
				string cleanLine = removeInlineComment(line);
				string lowerLine = lower(trim(cleanLine));

				if (startsWith(lowerLine, "setup_loop")) {
					setup_loop_detected = true;
					auto parts = split(cleanLine, ',');
					if (parts.size() < 2 || parts.size() > 3) {
						throw runtime_error("Invalid setup_loop directive: " + line);
					}

					auto firstPartWords = split(trim(parts[0]), ' ');
					string src = firstPartWords.size() > 1 ? firstPartWords[1] : "";
					string src_backup = trim(parts[1]);
					string label = (parts.size() == 3 && trim(parts[2]) != "none") ? trim(parts[2]) : "home";

					modified.push_back("restore:");
					modified.push_back("    setlr");
					modified.push_back("    DI,RT");
					modified.push_back("    xr0 = adr_of length, 0x01, 0x00");
					modified.push_back("    [er0] = er2,rt");
					modified.push_back("    qr0 = pr_length, " + src_backup + ", " + src + ", 0x0000");
					modified.push_back("    0x8932");
					modified.push_back("length:");
					modified.push_back("    0x0800");
					modified.push_back("    0x0000");
					modified.push_back("set_sp:");
					modified.push_back("    er6 = adr_of [-2] " + label);
					modified.push_back("    sp = er6,pop er8");
				}
				else {
					modified.push_back(line);
				}
			}
			return setup_loop_detected ? modified : program;
		}
	};

	// ==========================
	// Output emitter
	// ==========================

	class OutputEmitter {
	public:
		static void emitHex(const vector<int>& bytes) {
			for (size_t i = 0; i < bytes.size(); ++i) {
				if (i)
					cout << ' ';
				cout << uppercase << hex << setw(2) << setfill('0') << (bytes[i] & 0xff);
			}
			cout << dec << "\n";
		}

		static void emitKey(const vector<int>& bytes, const vector<int>& npress, const vector<string>& symbolrepr) {
			for (size_t i = 0; i < bytes.size(); ++i) {
				if (i)
					cout << ' ';
				cout << byteToKey(bytes[i] & 0xff, npress, symbolrepr);
			}
			cout << dec << "\n";
		}
	};

	// ==========================
	// Multi-section driver
	// ==========================

	struct CompileOptions {
		string target = "none";
		string format = "hex";
		int overflowInitialSp = 0x8154;
	};

	class Driver {
		CommandDatabase& db;
		vector<int> npress;
		vector<string> symbolrepr;

		int getNpressAdr(int adr) const {
			return npress[adr & 0xff] + npress[(adr >> 8) & 0xff];
		}

	public:
		explicit Driver(CommandDatabase& db) : db(db), npress(256, 1), symbolrepr(256, "") {
			for (int i = 0; i < 256; ++i) {
				symbolrepr[i] = "<" + hexWord(i, 2).substr(2) + ">";
			}
		}

		void setNpress(const vector<int>& customNpress) {
			if (customNpress.size() == 256)
				npress = customNpress;
		}

		void setSymbolrepr(const vector<string>& customSymbolrepr) {
			if (customSymbolrepr.size() == 256)
				symbolrepr = customSymbolrepr;
		}

		void compileProgram(const vector<string>& source, const CompileOptions& opt) {
			auto sections = SourcePreprocessor::splitSections(source);
			int previousLen = 0;

			for (size_t i = 0; i < sections.size(); ++i) {
				vector<string> modified_program = SourcePreprocessor::processSetupLoop(sections[i]);

				Compiler cc(db, npress);
				cc.setPreviousSectionLength(previousLen);
				cc.compileLines(modified_program);
				cc.finish();

				auto& st = cc.state();
				int home = 0;
				int home2 = 0;

				if (opt.target == "none" || opt.target == "overflow") {
					if (opt.target == "overflow" && st.result.size() > 100) {
						throw runtime_error("Program too long for overflow (max 100 bytes)");
					}

					if (st.home.has_value()) {
						home = *st.home;
					}
					else {
						home = opt.overflowInitialSp;
						if (st.labels.count("home")) {
							home -= st.labels["home"];
						}
						if (home + st.result.size() > 0x8E00) {
							cerr << "Warning: Program length after home > available space\n";
						}

						int min_home = home;
						while (min_home >= 0x8154 + 200)
							min_home -= 100;
						while (home + st.result.size() <= 0x8E00)
							home += 100;

						int best_home = min_home;
						int best_score = 9999999;
						int best_home_neg = -min_home;

						for (int h = min_home; h < home; h += 100) {
							int score = 0;
							for (auto& fixup : st.adrOfFixups) {
								int target_adr = h + st.labels[fixup.label] + fixup.offset;
								if (getNpressAdr(target_adr) >= 100)
									score += 1;
							}
							if (score < best_score || (score == best_score && (-h) < best_home_neg)) {
								best_score = score;
								best_home_neg = -h;
								best_home = h;
							}
						}
						home = best_home;
					}
				}
				else if (opt.target == "loader") {
					if (st.home.has_value()) {
						home = *st.home;
					}
					else {
						home = 0x85b0 - int(st.result.size());
						int entry = home + (st.labels.count("home") ? st.labels["home"] : 0) - 2;

						vector<int> loader_tail = {0x6a, 0x4f, 0, 0, entry & 255, (entry >> 8) & 255, 0x68, 0x4f, 0, 0};
						st.result.insert(st.result.end(), loader_tail.begin(), loader_tail.end());

						while (home + st.result.size() < 0x85d7) {
							st.result.push_back(0);
						}
						st.result.insert(st.result.end(), {0xff, 0xae, 0x85});

						home2 = 0;
						if (home - home2 < 0x8501)
							throw runtime_error("Program too long for loader mode");

						while (getNpressAdr(home - home2) >= 100) {
							home2 += 1;
						}
					}
				}
				else {
					throw runtime_error("Internal error: Unknown target");
				}

				cc.resolveAdrOf(home);

				cout << "\n=== Section " << (i + 1) << "/" << sections.size() << " ===\n";
				if (i > 0) {
					cout << "Previous section length: 0x" << hex << uppercase << previousLen << dec
						 << " (" << previousLen << " bytes)\n";
				}

				for (auto const& [label, offset] : st.labels) {
					cout << "The " << label << " code is being redirected to address 0x"
						 << hex << uppercase << (home + offset) << dec << ".\n";
				}

				cout << "Length (HEX): 0x" << hex << uppercase << st.result.size()
					 << dec << " bytes | (DEC): " << st.result.size() << " bytes\n";

				if (opt.target == "overflow") {
					vector<int> hackstring(100);
					string pattern = "1234567890";
					for (int k = 0; k < 100; ++k)
						hackstring[k] = pattern[k % 10];

					for (size_t k = 0; k < st.result.size(); ++k) {
						hackstring[(home + k - 0x8154) % 100] = st.result[k];
					}

					if (opt.format == "hex") {
						OutputEmitter::emitHex(hackstring);
					}
					else if (opt.format == "key") {
						OutputEmitter::emitKey(hackstring, npress, symbolrepr);
					}
				}
				else if (opt.target == "none") {
					if (opt.format == "hex") {
						int addr_val = st.orgAddress.has_value() ? (*st.orgAddress + st.result.size()) : (home + st.result.size());
						cout << "=====0x" << hex << uppercase << home << " -> 0x" << addr_val << dec << "=====\n";
						OutputEmitter::emitHex(st.result);
						cout << "======================\n";
					}
					else if (opt.format == "key") {
						cout << "0x" << hex << home << dec << ": ";
						OutputEmitter::emitKey(st.result, npress, symbolrepr);
					}
				}
				else if (opt.target == "loader") {
					if (opt.format == "key") {
						cout << "Address to load: "
							 << byteToKey((home - home2) & 255, npress, symbolrepr) << " "
							 << byteToKey((home - home2) >> 8, npress, symbolrepr) << "\n";

						vector<int> final_result(home2, 0);
						final_result.insert(final_result.end(), st.result.begin(), st.result.end());

						OutputEmitter::emitKey(final_result, npress, symbolrepr);
					}
					else {
						OutputEmitter::emitHex(st.result);
					}
				}

				previousLen = int(st.result.size());

				if (i < sections.size() - 1) {
					cout << "\n============================================================\n";
				}
			}
		}
	};

} // namespace lc::details

namespace lc {
    using Compiler = details::Compiler;
    using CompilerState = details::CompilerState;
    using CompileOptions = details::CompileOptions;
    using Driver = details::Driver;
    using CommandDatabase = details::CommandDatabase;
    using FontTable = details::FontTable;
    using CalcTable = details::CalcTable;
}aLabels_; }
};

// ==========================
// Disassembly Database
// ==========================

class DisassemblyDatabase {
    vector<string> entries_;
public:
    void load(const string& filename) {
        ifstream f(filename);
        if (!f) throw runtime_error("Cannot open disassembly file: " + filename);
        string line;
        regex lineRegex(R"(\t(.*?)\s*; ([0-9a-fA-F]*) \|)");
        while (getline(f, line)) {
            smatch match;
            if (regex_match(line, match, lineRegex)) {
                int addr = stoi(match[2].str(), nullptr, 16);
                while (addr >= (int)entries_.size()) entries_.push_back("");
                entries_[addr] = match[1].str();
            }
        }
    }
    const string& at(int addr) const {
        static string empty;
        if (addr < 0 || addr >= (int)entries_.size()) return empty;
        return entries_[addr];
    }
    const vector<string>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }
};

// ==========================
// Definition Database
// ==========================

struct DefEntry {
    vector<string> params;
    vector<string> body;
};

class DefinitionDatabase {
    unordered_map<string, DefEntry> defs_;
public:
    void load(const string& path) {
        ifstream f(path);
        if (!f) throw runtime_error("Cannot open defs file: " + path);
        vector<string> lines;
        string line;
        while (getline(f, line)) lines.push_back(rtrim(line));

        int i = 0;
        while (i < (int)lines.size()) {
            string l = trim(lines[i]);
            if (startsWith(l, "def ") && endsWith(l, ":")) {
                string header = l.substr(4, l.size() - 5);
                size_t paren = header.find('(');
                if (paren == string::npos) { ++i; continue; }
                string name = canonicalize(header.substr(0, paren));
                string rawArgs = header.substr(paren + 1);
                if (!rawArgs.empty() && rawArgs.back() == ')') rawArgs.pop_back();
                vector<string> params;
                if (!rawArgs.empty())
                    for (auto& p : split(rawArgs, ','))
                        params.push_back(canonicalize(trim(p)));
                vector<string> body;
                ++i;
                while (i < (int)lines.size()) {
                    string nextLine = lines[i];
                    if (nextLine.empty() || (!nextLine.starts_with(' ') && !nextLine.starts_with('\t')))
                        break;
                    body.push_back(canonicalize(trim(nextLine)));
                    ++i;
                }
                defs_[lower(name)] = DefEntry{params, body};
            }
            else ++i;
        }
    }
    bool hasDef(const string& name) const { return defs_.count(lower(name)); }
    const DefEntry& getDef(const string& name) const {
        auto it = defs_.find(lower(name));
        if (it == defs_.end()) throw runtime_error("unknown def: " + name);
        return it->second;
    }
    const unordered_map<string, DefEntry>& defs() const { return defs_; }
};

// ==========================
// Extension System
// ==========================

struct Extension {
    string syntax;
    string logic;
    vector<string> output;
    regex pattern;

    void compilePattern() {
        string pat = regex_replace(syntax, regex(R"(\{(\w+)\})"), "(?P<$1>.+?)");
        pat = regex_replace(pat, regex(R"(\(\?P<)"), "(?<");
        try { pattern = regex(pat); }
        catch (const regex_error& e) {
            throw runtime_error("Invalid extension pattern: " + pat + " - " + e.what());
        }
    }
};

class ExtensionManager {
    vector<Extension> extensions_;
public:
    void load(const string& path) {
        ifstream f(path);
        if (!f) { cerr << "[WARN] No extension file found: " + path + "\n"; return; }
        string content((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
        regex pattern(R"(---syntax---\s*(.*?)\s*---logic---\s*(.*?)\s*---output---\s*(.*?)\s*(?=---syntax---|$))");
        auto mb = sregex_iterator(content.begin(), content.end(), pattern);
        auto me = sregex_iterator();
        for (auto it = mb; it != me; ++it) {
            const smatch& m = *it;
            Extension ext;
            ext.syntax = trim(m[1].str());
            ext.logic = trim(m[2].str());
            string outputBlock = trim(m[3].str());
            for (auto& ln : split(outputBlock, '\n')) {
                string t = trim(ln);
                if (!t.empty()) ext.output.push_back(t);
            }
            ext.compilePattern();
            extensions_.push_back(std::move(ext));
        }
        sort(extensions_.begin(), extensions_.end(),
            [](const Extension& a, const Extension& b) { return a.syntax.size() > b.syntax.size(); });
    }

    vector<string> expand(const vector<string>& programLines,
        function<map<string, string>(const string& logic, const Extension& ext)> logicHandler = nullptr) const {
        vector<string> expanded;
        for (string line : programLines) {
            size_t dashPos = line.find("---");
            if (dashPos != string::npos) line = line.substr(0, dashPos);
            line = trim(line);
            if (line.empty()) continue;

            bool matchedFull = false;
            for (const auto& ext : extensions_) {
                smatch match;
                bool isInline = false;
                if (!regex_match(line, match, ext.pattern)) {
                    if (!regex_search(line, match, ext.pattern)) continue;
                    isInline = true;
                }
                map<string, string> localEnv;
                for (size_t i = 1; i < match.size(); ++i)
                    localEnv["_" + to_string(i)] = match[i].str();

                if (logicHandler && !ext.logic.empty()) {
                    auto lr = logicHandler(ext.logic, ext);
                    localEnv.insert(lr.begin(), lr.end());
                }

                vector<string> outputLines;
                for (const string& out : ext.output) {
                    string temp = out;
                    for (auto& [k, v] : localEnv)
                        temp = replaceAll(temp, "{" + k + "}", v);
                    outputLines.push_back(temp);
                }
                if (isInline && outputLines.size() == 1) {
                    line = line.substr(0, match.position()) + outputLines[0]
                        + line.substr(match.position() + match.length());
                }
                else {
                    expanded.insert(expanded.end(), outputLines.begin(), outputLines.end());
                    matchedFull = true;
                    break;
                }
            }
            if (!matchedFull) expanded.push_back(line);
        }
        return expanded;
    }

    const vector<Extension>& extensions() const { return extensions_; }
};

// ==========================
// Variable Store
// ==========================

class VariableStore {
    unordered_map<string, string> vars_;
    unordered_map<string, string> stringVars_;
public:
    void setVar(const string& name, const string& value) { vars_[name] = value; }
    string getVar(const string& name) const {
        auto it = vars_.find(name);
        if (it == vars_.end()) throw runtime_error("unknown variable: " + name);
        return it->second;
    }
    bool hasVar(const string& name) const { return vars_.count(name); }
    void clearVar(const string& name) { vars_.erase(name); }
    void setStringVar(const string& name, const string& value) { stringVars_[name] = value; }
    string getStringVar(const string& name) const {
        auto it = stringVars_.find(name);
        if (it == stringVars_.end()) throw runtime_error("unknown string variable: " + name);
        return it->second;
    }
    bool hasStringVar(const string& name) const { return stringVars_.count(name); }

    int resolveIntOrVar(const string& s) const {
        string t = trim(s);
        bool negative = false;
        if (!t.empty() && t[0] == '-') {
            negative = true;
            t = t.substr(1);
        }
        try {
            long long val = parseInteger(t, 0);
            return negative ? (int)-val : (int)val;
        } catch (...) {
            if (hasVar(t)) {
                string v = trim(getVar(t));
                try {
                    long long val = parseInteger(v, 0);
                    return negative ? (int)-val : (int)val;
                } catch (...) {
                    throw runtime_error("Variable '" + t + "' value '" + v + "' is not a valid integer");
                }
            }
            throw runtime_error("Unknown integer or variable: '" + trim(s) + "'");
        }
    }

    string resolveIndex(const string& value, int index) const {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            string inner = value.substr(1, value.size() - 2);
            if (index >= 0 && index < (int)inner.size())
                return string("\"") + inner[index] + "\"";
            return "";
        }
        if (value.find(';') != string::npos) {
            auto items = split(value, ';');
            vector<string> filtered;
            for (auto& item : items) { string t = trim(item); if (!t.empty()) filtered.push_back(t); }
            if (index >= 0 && index < (int)filtered.size()) return filtered[index];
            return "";
        }
        return value;
    }

    string expandVariables(const string& line) const {
        string expanded = line;

        regex indexRegex(R"(\b(\w+)\[(\d+)\])");
        string temp;
        sregex_iterator it(expanded.begin(), expanded.end(), indexRegex);
        sregex_iterator end_it;
        size_t lastPos = 0;
        for (; it != end_it; ++it) {
            const smatch& m = *it;
            temp.append(expanded, lastPos, m.position() - lastPos);
            string vn = m[1].str();
            int idx = stoi(m[2].str());
            if (hasVar(vn)) temp += resolveIndex(getVar(vn), idx);
            else temp += m[0].str();
            lastPos = m.position() + m.length();
        }
        temp.append(expanded, lastPos, string::npos);
        expanded = std::move(temp);

        regex varRegex(R"(\b\w+\b)");
        temp.clear();
        it = sregex_iterator(expanded.begin(), expanded.end(), varRegex);
        lastPos = 0;
        for (; it != end_it; ++it) {
            const smatch& m = *it;
            temp.append(expanded, lastPos, m.position() - lastPos);
            string vn = m[0].str();
            if (hasVar(vn)) temp += getVar(vn);
            else temp += m[0].str();
            lastPos = m.position() + m.length();
        }
        temp.append(expanded, lastPos, string::npos);

        return temp;
    }

    string expandVarsInExpr(const string& expr) const {
        string expanded = expr;
        for (const auto& [name, value] : vars_) {
            string pat = "\\b" + regexEscape(name) + "\\b";
            try {
                expanded = regex_replace(expanded, regex(pat), value);
            } catch (...) {}
        }
        return expanded;
    }

    const unordered_map<string, string>& vars() const { return vars_; }
    const unordered_map<string, string>& stringVars() const { return stringVars_; }
};

// ==========================
// User Function (callback-based replacement for Python exec)
// ==========================

using UserFunction = function<vector<string>(const vector<string>& args)>;

class UserFunctionStore {
    unordered_map<string, UserFunction> functions_;
public:
    void registerFunction(const string& name, UserFunction fn) {
        functions_[lower(name)] = std::move(fn);
    }
    bool hasFunction(const string& name) const { return functions_.count(lower(name)); }
    vector<string> callFunction(const string& name, const vector<string>& args) const {
        auto it = functions_.find(lower(name));
        if (it == functions_.end()) throw runtime_error("unknown user function: " + name);
        return it->second(args);
    }
};

// ==========================
// Calc Table (configurable)
// ==========================

struct CalcTable {
    unordered_map<string, int> tokens;
    vector<string> keys;
};

// ==========================
// AST Nodes
// ==========================

struct EmptyStmt {};
struct LabelStmt { string name; };
struct HexLiteralStmt { string text; };
struct RawHexStmt { string hex; };
struct CallStmt { string target; };
struct GotoStmt { string label; };
struct NextStmt { string label; };
struct AdrOfStmt { string offsetExpr; string baseAddrExpr; string label; string bracketSpec; };
struct AdrStmt { string label; string offsetExpr; string baseAddrExpr; };
struct JumpStmt { string offsetExpr; string baseAddrExpr; string label; };
struct AdrArithStmt { string leftOffsetExpr; string leftLabel; string rightOffsetExpr; string rightLabel; };
struct SizeLengthStmt { string leftLabel; string rightLabel; string leftOffsetExpr; string rightOffsetExpr; };
struct AssignmentStmt { string target; string valueText; };
struct OrgStmt { string expr; };
struct PrLengthStmt {};
struct RemainingLengthStmt {};
struct LBytesStmt {};
struct BackupIsStmt { string value; };
struct AddrCopyIsStmt { string value; };
struct MacroStmt { string name; };
struct StrStoreStmt { string name; string text; };
struct StrEmitStmt { string text; };
struct StrUseStmt { string name; };
struct TokenLiteralStmt { string content; };
struct CalcStmt { string content; };
struct EvalStmt { string expr; };
struct VarAssignStmt { string name; string value; };
struct DataLabelStmt { string name; int offset = 0; };
struct RepeatStmt { string count; vector<string> body; };
struct FuncDefStmt { string name; vector<string> args; vector<string> body; };
struct UserFuncDefStmt { string name; vector<string> args; vector<string> body; };
struct DefAliasStmt { string newName; string oldName; };
struct FreeformStmt { string text; };

using Stmt = variant<
    EmptyStmt, LabelStmt, HexLiteralStmt, RawHexStmt, CallStmt, GotoStmt,
    NextStmt, AdrOfStmt, AdrStmt, JumpStmt, AdrArithStmt, SizeLengthStmt,
    AssignmentStmt, OrgStmt, PrLengthStmt, RemainingLengthStmt, LBytesStmt,
    BackupIsStmt, AddrCopyIsStmt, MacroStmt, StrStoreStmt, StrEmitStmt,
    StrUseStmt, TokenLiteralStmt, CalcStmt, EvalStmt, VarAssignStmt,
    DataLabelStmt, RepeatStmt, FuncDefStmt, UserFuncDefStmt,
    DefAliasStmt, FreeformStmt>;

// ==========================
// Parser
// ==========================

class Parser {
    Diagnostic diag_;
    const CommandDatabase* db_;
    const DefinitionDatabase* defs_;

public:
    explicit Parser(const CommandDatabase* db = nullptr,
        const DefinitionDatabase* defs = nullptr,
        Diagnostic d = {})
        : diag_(std::move(d)), db_(db), defs_(defs) {}

    Stmt parseLine(string line) {
        line = removeInlineComment(std::move(line));
        line = trim(line);
        if (line.empty()) return EmptyStmt{};

        string low = lower(line);

        if (db_ && db_->hasCommand(low)) return CallStmt{low};

        if (startsWith(low, "lbl ")) return LabelStmt{lower(trim(line.substr(4)))};
        if (endsWith(line, ":")) return LabelStmt{lower(trim(line.substr(0, line.size() - 1)))};

        if (startsWith(low, "0x")) return HexLiteralStmt{low};
        if (startsWith(low, "hex") && low.find("hex_") == string::npos)
            return RawHexStmt{trim(line.substr(3))};

        if (startsWith(low, "call")) return CallStmt{lower(trim(line.substr(4)))};
        if (startsWith(low, "goto")) return GotoStmt{lower(trim(line.substr(4)))};
        if (startsWith(low, "next")) return NextStmt{lower(trim(line.substr(4)))};

        if (startsWith(low, "adr_of")) return parseAdrOf(trim(line.substr(6)));
        if (startsWith(low, "jump")) return parseJump(trim(line.substr(4)));

        if (startsWith(low, "adr(") && line.find(')') != string::npos)
            return parseAdrStmt(line);
        if (startsWith(low, "adr_arith")) return parseAdrArith(low);

        if ((startsWith(low, "size(") || startsWith(low, "length(")) && endsWith(low, ")"))
            return parseSizeLength(low);

        if (startsWith(low, "org")) return OrgStmt{trim(line.substr(3))};
        if (startsWith(low, "pr_length")) return PrLengthStmt{};
        if (startsWith(low, "remaining_length")) return RemainingLengthStmt{};
        if (startsWith(low, "l_bytes")) return LBytesStmt{};

        if (startsWith(low, "backup is ")) return BackupIsStmt{trim(line.substr(10))};
        if (startsWith(low, "addrcopy is ")) return AddrCopyIsStmt{trim(line.substr(12))};

        if (startsWith(low, "loop880") || startsWith(low, "loop580") ||
            startsWith(low, "backup580") || startsWith(low, "backup880"))
            return MacroStmt{low};

        if (startsWith(low, "str")) return parseStringStmt(line);

        if (startsWith(low, "calc ")) return CalcStmt{trim(line.substr(5))};
        if (startsWith(low, "calc(") && endsWith(low, ")"))
            return CalcStmt{trim(line.substr(5, line.size() - 6))};

        if (startsWith(low, "eval(") && endsWith(low, ")"))
            return EvalStmt{trim(line.substr(5, line.size() - 6))};

        if (line.size() >= 2 && line[0] == '\'' && line.back() == '\'')
            return TokenLiteralStmt{line.substr(1, line.size() - 2)};

        if (line[0] == '"') return parseQuotedString(line);

        if (startsWith(low, "def ") && line.find('=') != string::npos) {
            smatch m;
            if (regex_match(line, m, regex(R"(^\s*(\w+)\s+(\w+)\s*=\s*(.+)$)")))
                return DefAliasStmt{lower(m[2].str()), lower(trim(m[3].str()))};
        }
        if (startsWith(low, "def ") && endsWith(low, "{"))
            return UserFuncDefStmt{lower(trim(low.substr(4))), {}, {}};
        if (startsWith(low, "func "))
            return FuncDefStmt{lower(trim(low.substr(5))), {}, {}};

        if (startsWith(low, "repeat ") || startsWith(low, "loop "))
            return RepeatStmt{low, {}};

        size_t eq = line.find('=');
        if (eq != string::npos) {
            string target = lower(trim(line.substr(0, eq)));
            string value = trim(line.substr(eq + 1));
            if (startsWith(target, "var "))
                return VarAssignStmt{trim(target.substr(4)), value};
            return AssignmentStmt{target, lower(value)};
        }

        if (db_ && db_->hasDataLabel(low)) return DataLabelStmt{low, 0};

        size_t plus = low.find('+');
        if (plus != string::npos && db_) {
            string name = trim(low.substr(0, plus));
            if (db_->hasDataLabel(name))
                return DataLabelStmt{name, (int)parseInteger(trim(low.substr(plus + 1)), 0)};
        }

        return FreeformStmt{low};
    }

    static vector<string> collectBlock(vector<string>::const_iterator& it, vector<string>::const_iterator end) {
        vector<string> body;
        int depth = 1;
        while (it != end) {
            string raw = *it; ++it;
            string s = trim(raw.substr(0, raw.find("---")));
            if (s.empty()) continue;
            if (s == "}") { if (--depth <= 0) break; body.push_back(s); continue; }
            depth += (int)count(s.begin(), s.end(), '{') - (int)count(s.begin(), s.end(), '}');
            body.push_back(s);
        }
        return body;
    }

private:
    tuple<string, string, string> parseAdrOfBracketSyntax(const string& spec, optional<int> /*currentHome*/) const {
        string s = trim(spec);
        if (s.empty()) diag_.error("adr_of/jump requires label or bracket arguments");
        if (s[0] != '[') return {"0", "", lower(trim(s))};

        vector<string> parts;
        regex bracketRegex(R"(\[([^\]]+)\])");
        auto mb = sregex_iterator(s.begin(), s.end(), bracketRegex);
        auto me = sregex_iterator();
        for (auto it = mb; it != me; ++it) parts.push_back((*it)[1].str());

        string trailingLabel = regex_replace(s, regex(R"(^(?:\[[^\]]+\])+)"), "");
        trailingLabel = trim(trailingLabel);
        bool firstIsLabel = !parts.empty() && !isNumericStr(parts[0]);

        if (!trailingLabel.empty()) firstIsLabel = false;

        if (firstIsLabel) {
            if (!trailingLabel.empty())
                diag_.error("adr_of: label in brackets and trailing");
            string label = lower(trim(parts[0]));
            string offsetExpr = "0";
            string baseAddrExpr = "";
            if (parts.size() >= 2) offsetExpr = trim(parts[1]);
            if (parts.size() >= 3) baseAddrExpr = trim(parts[2]);
            if (parts.size() > 3) diag_.error("adr_of: too many brackets");
            return {offsetExpr, baseAddrExpr, label};
        }

        if (!trailingLabel.empty()) {
            if (trailingLabel.size() >= 2 && trailingLabel.front() == '<' && trailingLabel.back() == '>')
                trailingLabel = trailingLabel.substr(1, trailingLabel.size() - 2);
            string label = lower(trim(trailingLabel));
            string offsetExpr = "0";
            string baseAddrExpr = "";
            if (parts.size() >= 1) offsetExpr = trim(parts[0]);
            if (parts.size() >= 2) baseAddrExpr = trim(parts[1]);
            if (parts.size() > 2) diag_.error("adr_of trailing-label: too many brackets");
            return {offsetExpr, baseAddrExpr, label};
        }

        string label = lower(trim(parts[0]));
        string offsetExpr = "0";
        string baseAddrExpr = "";
        if (parts.size() >= 2) offsetExpr = trim(parts[1]);
        if (parts.size() >= 3) baseAddrExpr = trim(parts[2]);
        if (parts.size() > 3) diag_.error("adr_of: too many brackets");
        return {offsetExpr, baseAddrExpr, label};
    }

    Stmt parseAdrOf(const string& body) {
        auto [offsetExpr, baseAddrExpr, label] = parseAdrOfBracketSyntax(body, nullopt);
        if (label.empty()) diag_.error("adr_of requires a label");
        return AdrOfStmt{offsetExpr, baseAddrExpr, label, body};
    }

    Stmt parseJump(const string& body) {
        auto [offsetExpr, baseAddrExpr, label] = parseAdrOfBracketSyntax(body, nullopt);
        if (label.empty()) diag_.error("jump requires a label");
        return JumpStmt{offsetExpr, baseAddrExpr, label};
    }

    Stmt parseAdrStmt(const string& line) {
        size_t openParen = line.find('(');
        size_t closeParen = line.rfind(')');
        string inner = trim(line.substr(openParen + 1, closeParen - openParen - 1));
        string remainder = trim(line.substr(closeParen + 1));

        string label; string offsetExpr = "0"; string baseAddrExpr = "";
        if (inner.find(',') != string::npos) {
            auto parts = split(inner, ',');
            label = lower(trim(parts[0]));
            if (parts.size() >= 2) offsetExpr = trim(parts[1]);
            if (parts.size() >= 3) baseAddrExpr = trim(parts[2]);
        } else if (!remainder.empty()) {
            label = lower(trim(inner));
            vector<string> bracketParts;
            regex bracketRegex(R"(\[([^\]]+)\])");
            auto mb = sregex_iterator(remainder.begin(), remainder.end(), bracketRegex);
            auto me = sregex_iterator();
            for (auto it = mb; it != me; ++it) bracketParts.push_back((*it)[1].str());
            if (bracketParts.size() >= 1) offsetExpr = trim(bracketParts[0]);
            if (bracketParts.size() >= 2) baseAddrExpr = trim(bracketParts[1]);
        } else {
            label = lower(trim(inner));
        }
        return AdrStmt{label, offsetExpr, baseAddrExpr};
    }

    static pair<string, string> parseAdrPart(string part) {
        part = trim(part);
        if (startsWith(part, "adr_arith")) part = trim(part.substr(9));
        string offsetExpr = "0"; string label;
        size_t lb = part.find('['), rb = part.find(']');
        if (lb != string::npos && rb != string::npos && rb > lb) {
            offsetExpr = trim(part.substr(lb + 1, rb - lb - 1));
            label = trim(part.substr(rb + 1));
        } else label = trim(part);
        if (label.empty()) throw runtime_error("adr_arith part missing label");
        return {offsetExpr, lower(label)};
    }

    Stmt parseAdrArith(const string& line) {
        int lastMinus = (int)line.size() - 1;
        while (lastMinus > 0) {
            if (line[lastMinus] == '-' && line.find("adr_arith", lastMinus) != string::npos) break;
            --lastMinus;
        }
        if (lastMinus <= 0) diag_.error("wrong adr_arith syntax");
        string left = trim(line.substr(9, lastMinus - 9));
        string right = trim(line.substr(lastMinus + 1));
        auto [loe, ll] = parseAdrPart(left);
        auto [roe, rl] = parseAdrPart(right);
        return AdrArithStmt{loe, ll, roe, rl};
    }

    Stmt parseSizeLength(const string& line) {
        smatch m;
        if (!regex_match(line, m, regex(R"(^(?:size|length)\((.+)\)$)")))
            diag_.error("Invalid size/length syntax: " + line);
        string content = m[1].str();
        int sep = -1;
        for (int idx = (int)content.size() - 1; idx > 0; --idx) {
            if (content[idx] == '-') { sep = idx; break; }
        }
        if (sep <= 0) diag_.error("size/length needs exactly 1 '-' separating 2 labels");
        string leftRaw = trim(content.substr(0, sep));
        string rightRaw = trim(content.substr(sep + 1));

        auto parsePart = [this](string part) -> pair<string, string> {
            part = trim(part);
            if (part.size() >= 2 && part.front() == '[' && part.back() == ']') {
                string inner = part.substr(1, part.size() - 2);
                if (isNumericStr(inner))
                    diag_.error("size/length: bracket contains number, not label");
                return {"0", lower(trim(inner))};
            }
            return {"0", lower(part)};
        };

        auto [loe, ll] = parsePart(leftRaw);
        auto [roe, rl] = parsePart(rightRaw);
        return SizeLengthStmt{ll, rl, loe, roe};
    }

    Stmt parseStringStmt(const string& line) {
        string content = trim(line.substr(3));
        size_t q = content.find('"');
        if (q != string::npos) {
            string name = trim(content.substr(0, q));
            Lexer lx(content.substr(q));
            Token t = lx.next();
            if (t.kind != TokenKind::String) diag_.error("invalid str string literal");
            if (name.empty()) return StrEmitStmt{t.text};
            else return StrStoreStmt{name, t.text};
        }
        if (!content.empty()) return StrUseStmt{content};
        diag_.error("wrong str syntax");
        return EmptyStmt{};
    }

    Stmt parseQuotedString(const string& line) {
        Lexer lx(line);
        Token t = lx.next();
        if (t.kind != TokenKind::String) diag_.error("invalid quoted string literal");
        return StrEmitStmt{t.text};
    }
};

// ==========================
// Compiler State & Fixups
// ==========================

struct AdrOfFixup {
    int pos;
    string offsetExpr;
    string baseAddrExpr;
    string label;
};

struct AdrArithFixup {
    int pos;
    string leftOffsetExpr;
    string leftLabel;
    string rightOffsetExpr;
    string rightLabel;
};

struct DeferredEvalFixup {
    int pos;
    string expr;
};

struct PrLengthFixup {
    int pos;
};

struct RemainingLengthFixup {
    int pos;
};

struct CompiledSegment {
    int baseAddress = 0;
    vector<int> bytes;
};

struct CompilerState {
    vector<int> result;
    unordered_map<string, int> labels;
    VariableStore vars;
    unordered_map<string, string> stringVars;

    vector<AdrOfFixup> adrOfFixups;
    vector<AdrArithFixup> adrArithFixups;
    vector<PrLengthFixup> prLengthFixups;
    vector<RemainingLengthFixup> remainingLengthFixups;
    vector<DeferredEvalFixup> deferredEvalFixups;

    optional<int> home;
    int hx = 0;
    string addrcopy;
    string backup;

    FontTable* fontTable = nullptr;
    CommandDatabase* db = nullptr;
    const CalcTable* calcTable = nullptr;
    Diagnostic diag;
};

struct CompileOptions {
    string target = "none";
};

class Driver {
public:
    // Stub for UI compatibility
};

// ==========================
// Compiler
// ==========================

class Compiler {
    CompilerState state_;
    CommandDatabase* db_;
    const DefinitionDatabase* defs_ = nullptr;
    const KeypressTable* keypressTable_ = nullptr;

public:
    Compiler(CommandDatabase& db) : db_(&db) {
        state_.db = &db;
    }

    void setFontTable(FontTable* ft) { state_.fontTable = ft; }
    void setCalcTable(const CalcTable* ct) { state_.calcTable = ct; }
    void setDefDB(const DefinitionDatabase* d) { defs_ = d; }
    void setKeypressTable(const KeypressTable* kt) { keypressTable_ = kt; }

    CompilerState& state() { return state_; }

    // ── Helpers ──────────────────────────────────────────────

    int resolveOffset(const string& offsetExpr) const {
        string t = trim(offsetExpr);
        if (t.empty() || t == "0") return 0;
        return state_.vars.resolveIntOrVar(offsetExpr);
    }

    void emitByte(int b) { state_.result.push_back(b & 0xFF); }

    void emitWord(int w) {
        state_.result.push_back(w & 0xFF);
        state_.result.push_back((w >> 8) & 0xFF);
    }

    void emitDWord(int d) {
        state_.result.push_back(d & 0xFF);
        state_.result.push_back((d >> 8) & 0xFF);
        state_.result.push_back((d >> 16) & 0xFF);
        state_.result.push_back((d >> 24) & 0xFF);
    }

    void emitHexLiteral(const string& text) {
        string hexStr = text.substr(2); // skip "0x"
        if (hexStr.empty()) return;
        if (hexStr.size() % 2 != 0) hexStr = "0" + hexStr;
        int nBytes = (int)hexStr.size() / 2;
        unsigned int data = (unsigned int)stoul(hexStr, nullptr, 16);
        for (int i = 0; i < nBytes; ++i) {
            state_.result.push_back(data & 0xFF);
            data >>= 8;
        }
    }

    void emitHexString(const string& hexData) {
        string d = hexData;
        d.erase(remove_if(d.begin(), d.end(), ::isspace), d.end());
        if (d.size() % 2 != 0) throw runtime_error("Invalid hex data length");
        for (size_t i = 0; i < d.size(); i += 2)
            state_.result.push_back(stoi(d.substr(i, 2), nullptr, 16));
    }

    // ── Eval Expression Engine ───────────────────────────────

    string resolveAdrOfInExpr(const string& expr) const {
        regex pattern(R"(adr_of\s*((?:\[[^\]]+\]\s*|<(?:[^>]+)>\s*|[a-zA-Z_]\w*\s*)+))");
        string result_str = expr;

        result_str = regexReplaceLambda(expr, pattern, [&](const smatch& m) -> string {
            string spec = trim(m[1].str());
            string label;
            string offsetExpr = "0";

            if (!spec.empty() && spec[0] == '[') {
                regex brk(R"(\[([^\]]+)\])");
                auto bm = sregex_iterator(spec.begin(), spec.end(), brk);
                auto be = sregex_iterator();
                vector<string> parts;
                for (auto bi = bm; bi != be; ++bi) parts.push_back((*bi)[1].str());
                if (!parts.empty()) {
                    if (!isNumericStr(parts[0])) {
                        label = lower(trim(parts[0]));
                        if (parts.size() >= 2) offsetExpr = trim(parts[1]);
                    } else {
                        offsetExpr = trim(parts[0]);
                    }
                }
            } else {
                label = lower(trim(spec));
            }

            if (label.empty()) return m[0].str();
            auto lit = state_.labels.find(label);
            if (lit == state_.labels.end())
                throw runtime_error("Label '" + label + "' not found for adr_of in eval expression");
            int offset = resolveOffset(offsetExpr);
            int addr = (state_.home.value_or(0)) + lit->second + offset;
            return to_string(addr);
        });
        return result_str;
    }

    long long evalSimpleExpr(string expr) const {
        expr = state_.vars.expandVarsInExpr(expr);
        expr = resolveAdrOfInExpr(expr);

        if (expr.find("adr(") != string::npos)
            throw runtime_error("Cannot evaluate expression with unresolved adr(): " + expr);

        size_t pos = 0;
        auto skipWs = [&]() { while (pos < expr.size() && isspace((unsigned char)expr[pos])) ++pos; };

        function<long long()> parseExpr;
        function<long long()> parseTerm;
        function<long long()> parseFactor;
        function<long long()> parseUnary;
        function<long long()> parsePrimary;

        parsePrimary = [&]() -> long long {
            skipWs();
            if (pos < expr.size() && expr[pos] == '(') {
                ++pos;
                long long v = parseExpr();
                skipWs();
                if (pos < expr.size() && expr[pos] == ')') ++pos;
                return v;
            }
            size_t start = pos;
            if (pos < expr.size() && (expr[pos] == '-' || expr[pos] == '+')) ++pos;
            while (pos < expr.size() && (isxdigit((unsigned char)expr[pos]) || expr[pos] == 'x' || expr[pos] == 'X'))
                ++pos;
            string numStr = expr.substr(start, pos - start);
            if (numStr.empty()) throw runtime_error("Expected number in eval: " + expr + " at pos " + to_string(pos));
            return parseInteger(trim(numStr), 0);
        };

        parseUnary = [&]() -> long long {
            skipWs();
            if (pos < expr.size() && expr[pos] == '-') { ++pos; return -parseUnary(); }
            if (pos < expr.size() && expr[pos] == '~') { ++pos; return ~parseUnary(); }
            return parsePrimary();
        };

        parseFactor = [&]() -> long long {
            long long v = parseUnary();
            while (pos < expr.size()) {
                skipWs();
                if (pos < expr.size() && expr[pos] == '*') { ++pos; v *= parseUnary(); }
                else if (pos < expr.size() && expr[pos] == '/' && (pos + 1 >= expr.size() || expr[pos+1] != '/')) { ++pos; v /= parseUnary(); }
                else if (pos < expr.size() && expr[pos] == '%') { ++pos; v %= parseUnary(); }
                else break;
            }
            return v;
        };

        parseTerm = [&]() -> long long {
            long long v = parseFactor();
            while (pos < expr.size()) {
                skipWs();
                if (pos < expr.size() && expr[pos] == '&') { ++pos; v &= parseFactor(); }
                else if (pos < expr.size() && expr[pos] == '|') { ++pos; v |= parseFactor(); }
                else if (pos < expr.size() && expr[pos] == '^') { ++pos; v ^= parseFactor(); }
                else break;
            }
            return v;
        };

        parseExpr = [&]() -> long long {
            long long v = parseTerm();
            while (pos < expr.size()) {
                skipWs();
                if (pos < expr.size() && expr[pos] == '+') { ++pos; v += parseTerm(); }
                else if (pos < expr.size() && expr[pos] == '-') { ++pos; v -= parseTerm(); }
                else break;
            }
            return v;
        };

        return parseExpr();
    }

    // ── Compile single statement ─────────────────────────────

    void compileStmt(const Stmt& stmt) {
        visit(overloaded {
            [&](const EmptyStmt&) {},

            [&](const LabelStmt& s) {
                string name = lower(s.name);
                if (state_.labels.count(name))
                    state_.diag.error("Duplicate label: " + name);
                state_.labels[name] = (int)state_.result.size();
            },

            [&](const HexLiteralStmt& s) {
                string line = s.text;
                size_t plusPos = line.find('+');
                size_t minusPos = line.find('-', 2);
                if (plusPos != string::npos) {
                    string hexPart = line.substr(0, plusPos);
                    string decPart = line.substr(plusPos + 1);
                    string hexStr = hexPart.substr(2);
                    if (hexStr.size() % 2 != 0) hexStr = "0" + hexStr;
                    int nBytes = (int)hexStr.size() / 2;
                    long long initial = stoll(hexStr, nullptr, 16);
                    long long val = initial + stoll(decPart, nullptr, 0);
                    for (int i = 0; i < nBytes; ++i) { state_.result.push_back(val & 0xFF); val >>= 8; }
                } else if (minusPos != string::npos) {
                    string hexPart = line.substr(0, minusPos);
                    string decPart = line.substr(minusPos + 1);
                    string hexStr = hexPart.substr(2);
                    if (hexStr.size() % 2 != 0) hexStr = "0" + hexStr;
                    int nBytes = (int)hexStr.size() / 2;
                    long long initial = stoll(hexStr, nullptr, 16);
                    long long val = initial - stoll(decPart, nullptr, 0);
                    for (int i = 0; i < nBytes; ++i) { state_.result.push_back(val & 0xFF); val >>= 8; }
                } else {
                    emitHexLiteral(line);
                }
            },

            [&](const RawHexStmt& s) { emitHexString(s.hex); },

            [&](const CallStmt& s) {
                if (!state_.db) { state_.diag.error("No command database for call: " + s.target); return; }
                int adr;
                try { adr = (int)parseInteger(s.target, 16); }
                catch (...) {
                    auto& cmd = state_.db->getCommand(s.target);
                    adr = cmd.address;
                    for (const auto& tag : cmd.tags)
                        if (startsWith(tag, "warning")) cerr << tag << "\n";
                }
                if (adr < 0 || adr > 0x3ffff)
                    state_.diag.error("Invalid call address: " + hexWord(adr));
                long long callAddr = adr + 0x30300000LL;
                if (state_.db->hasDataLabel("input_range") || state_.db->hasDataLabel("input_area")) {
                    int inputRange = state_.db->hasDataLabel("input_range") ? state_.db->getDataLabel("input_range") : state_.db->getDataLabel("input_area");
                    if (state_.home.has_value() && *state_.home >= inputRange && *state_.home < inputRange + 0xc8) {
                        callAddr = adr + 0x30300000LL;
                    } else {
                        callAddr = adr + 0x00000000LL;
                    }
                }
                emitDWord((int)callAddr);
            },

            [&](const GotoStmt& s) {
                string expr = "adr(" + s.label + ") - 2";
                state_.deferredEvalFixups.push_back({(int)state_.result.size(), expr});
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const NextStmt& s) {
                state_.adrOfFixups.push_back({(int)state_.result.size(), "-2", "", s.label});
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const AdrOfStmt& s) {
                state_.adrOfFixups.push_back({(int)state_.result.size(), s.offsetExpr, s.baseAddrExpr, s.label});
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const AdrStmt& s) {
                if ((s.offsetExpr == "0" || trim(s.offsetExpr).empty()) && (s.baseAddrExpr.empty() || trim(s.baseAddrExpr).empty())) {
                    string expr = "adr(" + s.label + ")";
                    state_.deferredEvalFixups.push_back({(int)state_.result.size(), expr});
                } else {
                    string expr = "adr(" + s.label + "," + s.offsetExpr;
                    if (!s.baseAddrExpr.empty()) expr += "," + s.baseAddrExpr;
                    expr += ")";
                    state_.deferredEvalFixups.push_back({(int)state_.result.size(), expr});
                }
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const JumpStmt& s) {
                state_.adrOfFixups.push_back({(int)state_.result.size(), s.offsetExpr, s.baseAddrExpr, s.label});
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const AdrArithStmt& s) {
                state_.adrArithFixups.push_back({(int)state_.result.size(),
                    s.leftOffsetExpr, s.leftLabel,
                    s.rightOffsetExpr, s.rightLabel});
                state_.result.push_back(0);
            },

            [&](const SizeLengthStmt& s) {
                state_.adrArithFixups.push_back({(int)state_.result.size(),
                    s.leftOffsetExpr, s.leftLabel,
                    s.rightOffsetExpr, s.rightLabel});
                state_.result.push_back(0);
            },

            [&](const AssignmentStmt& s) {
                state_.vars.setVar(s.target, s.valueText);
            },

            [&](const OrgStmt& s) {
                int newHx;
                try {
                    newHx = state_.vars.resolveIntOrVar(s.expr);
                } catch (...) {
                    newHx = (int)evalSimpleExpr(s.expr);
                }
                state_.hx = newHx;
                int home1 = newHx - (int)state_.result.size();
                if (!state_.home.has_value()) state_.home = home1;
                else if (*state_.home != home1) state_.diag.warn("Inconsistent home value");
            },

            [&](const PrLengthStmt&) {
                state_.prLengthFixups.push_back({(int)state_.result.size()});
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const RemainingLengthStmt&) {
                state_.remainingLengthFixups.push_back({(int)state_.result.size()});
                state_.result.push_back(0);
                state_.result.push_back(0);
            },

            [&](const LBytesStmt&) {},

            [&](const BackupIsStmt& s) { state_.backup = s.value; },

            [&](const AddrCopyIsStmt& s) { state_.addrcopy = s.value; },

            [&](const MacroStmt& s) {
                string name = s.name;
                if (name == "loop880") {
                    processLine("set_segment:"); processLine("setlr"); processLine("di,rt");
                    processLine("call pop xr0"); processLine("adr_of length"); processLine("0x0001");
                    processLine("[er0]=er2,rt"); processLine("loop:"); processLine("call pop qr0");
                    processLine("adr_of " + state_.addrcopy); processLine(state_.backup);
                    processLine("pr_length"); processLine("adr_of [-2] " + state_.addrcopy);
                    processLine("hex e6 4d"); processLine("length:"); processLine("remaining_length");
                    processLine("0x0000"); processLine("call sp=er6,pop er8");
                } else if (name == "loop580") {
                    processLine("set_segment:"); processLine("setlr"); processLine("di,rt");
                    processLine("call pop xr0"); processLine("adr_of length"); processLine("0x0001");
                    processLine("[er0]=er2,rt"); processLine("call pop qr0"); processLine("pr_length");
                    processLine(state_.backup); processLine("adr_of " + state_.addrcopy);
                    processLine("adr_of [-2] " + state_.addrcopy); processLine("0x8932");
                    processLine("length:"); processLine("remaining_length"); processLine("0x0000");
                    processLine("sp=er6,pop er8");
                } else if (name == "backup580") {
                    processLine("backup:"); processLine("call pop xr0"); processLine(state_.backup);
                    processLine("adr_of " + state_.addrcopy); processLine("call 0x09450"); processLine("pr_length");
                } else if (name == "backup880") {
                    processLine("backup:"); processLine("call pop xr0"); processLine(state_.backup);
                    processLine("adr_of " + state_.addrcopy); processLine("call 0x14DE8"); processLine("pr_length");
                    processLine("0x0000");
                }
            },

            [&](const StrEmitStmt& s) {
                string text = s.text;
                text = regexReplaceLambda(text, regex(R"(\{([a-zA-Z_]\w*(?:\[\d+\])?)\})"), [&](const smatch& m) -> string {
                    string vn = m[1].str();
                    if (state_.vars.hasVar(vn)) return state_.vars.getVar(vn);
                    return m[0].str();
                });
                for (char& c : text) if (c == ' ') c = '~';
                try {
                    vector<int> bytes;
                    if (state_.fontTable) {
                        bytes = state_.fontTable->fromFont(text);
                    } else {
                        bytes = FontTable::encodeString(text);
                    }
                    state_.result.insert(state_.result.end(), bytes.begin(), bytes.end());
                } catch (const exception& e) {
                    state_.diag.error(e.what());
                }
            },

            [&](const StrStoreStmt& s) { state_.stringVars[s.name] = s.text; },

            [&](const StrUseStmt& s) {
                auto it = state_.stringVars.find(s.name);
                if (it == state_.stringVars.end()) state_.diag.error("Unknown string variable: " + s.name);
                string text = it->second;
                for (char& c : text) if (c == ' ') c = '~';
                try {
                    vector<int> bytes;
                    if (state_.fontTable) {
                        bytes = state_.fontTable->fromFont(text);
                    } else {
                        bytes = FontTable::encodeString(text);
                    }
                    state_.result.insert(state_.result.end(), bytes.begin(), bytes.end());
                } catch (const exception& e) {
                    state_.diag.error(e.what());
                }
            },

            [&](const TokenLiteralStmt& s) {
                try {
                    vector<int> bytes;
                    if (state_.fontTable) {
                        bytes = state_.fontTable->encodeTokens(s.content);
                    } else {
                        bytes = FontTable::encodeTokens(s.content);
                    }
                    state_.result.insert(state_.result.end(), bytes.begin(), bytes.end());
                } catch (const exception& e) {
                    state_.diag.error(e.what());
                }
            },

            [&](const CalcStmt& s) {
                if (!state_.calcTable) { state_.diag.error("No calc table"); return; }
                string content = s.content;
                if (content.size() >= 2 && content.front() == '"' && content.back() == '"')
                    content = content.substr(1, content.size() - 2);
                vector<int> encoded;
                size_t i = 0;
                while (i < content.size()) {
                    if (isspace((unsigned char)content[i])) { ++i; continue; }
                    bool found = false;
                    for (auto& key : state_.calcTable->keys) {
                        if (content.compare(i, key.size(), key) == 0) {
                            auto tit = state_.calcTable->tokens.find(key);
                            if (tit != state_.calcTable->tokens.end()) {
                                encoded.push_back(tit->second);
                                i += key.size();
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        unsigned char ch = content[i];
                        string sch(1, ch);
                        auto tit = state_.calcTable->tokens.find(sch);
                        if (tit != state_.calcTable->tokens.end()) {
                            encoded.push_back(tit->second);
                        } else if (isalpha(ch)) {
                            auto tit2 = state_.calcTable->tokens.find(string(1, (char)tolower(ch)));
                            if (tit2 != state_.calcTable->tokens.end())
                                encoded.push_back(tit2->second);
                            else
                                state_.diag.error("Unknown calc token: " + string(1, content[i]));
                        } else {
                            state_.diag.error("Unknown calc token: " + string(1, content[i]));
                        }
                        ++i;
                    }
                }
                string hexOut;
                for (int b : encoded) {
                    char buf[4]; snprintf(buf, sizeof(buf), "%02x", b & 0xFF); hexOut += buf;
                }
                if (!hexOut.empty()) emitHexString(hexOut);
            },

            [&](const EvalStmt& s) {
                string expanded = state_.vars.expandVarsInExpr(s.expr);

                regex nestedEval(R"(eval\(([^()]*(?:\([^()]*\)[^()]*)*)\))");
                int safety = 100;
                while (regex_search(expanded, nestedEval) && safety-- > 0) {
                    expanded = regexReplaceLambda(expanded, nestedEval, [&](const smatch& m) -> string {
                        string inner = m[1].str();
                        inner = state_.vars.expandVarsInExpr(inner);
                        if (inner.find("adr(") != string::npos || inner.find("adr_of") != string::npos)
                            return "(" + inner + ")";
                        try {
                            long long val = evalSimpleExpr(inner);
                            return to_string(val);
                        } catch (...) {
                            return "(" + inner + ")";
                        }
                    });
                }

                if (expanded.find("adr(") != string::npos || expanded.find("adr_of") != string::npos) {
                    state_.deferredEvalFixups.push_back({(int)state_.result.size(), expanded});
                    state_.result.push_back(0);
                    state_.result.push_back(0);
                    return;
                }

                try {
                    long long val = evalSimpleExpr(expanded);
                    emitWord((int)(val & 0xFFFF));
                } catch (const exception& e) {
                    state_.diag.error(string("Eval error: ") + e.what() + " in '" + s.expr + "'");
                }
            },

            [&](const VarAssignStmt& s) { state_.vars.setVar(s.name, s.value); },

            [&](const DataLabelStmt& s) {
                if (!state_.db) { state_.diag.error("No command database for data label"); return; }
                int addr = state_.db->getDataLabel(s.name) + s.offset;
                emitWord(addr);
            },

            [&](const DefAliasStmt& s) {
                if (!state_.db) return;
                if (state_.db->hasCommand(s.oldName)) {
                    auto& cmd = state_.db->getCommand(s.oldName);
                    state_.db->addCommand(cmd.address, s.newName, cmd.tags);
                } else if (state_.db->hasDataLabel(s.oldName)) {
                    state_.db->addDataLabel(s.newName, state_.db->getDataLabel(s.oldName));
                }
            },

            [&](const RepeatStmt&) {},
            [&](const FuncDefStmt&) {},
            [&](const UserFuncDefStmt&) {},

            [&](const FreeformStmt& s) {
                string expanded = state_.vars.expandVariables(s.text);
                if (expanded != s.text) {
                    processLine(expanded);
                } else {
                    state_.diag.error("Unrecognized command: " + s.text);
                }
            }
        }, stmt);
    }

    // ── Process a single line (parse + compile) ──────────────

    void processLine(const string& rawLine) {
        string line = rawLine;
        size_t dashPos = line.find("---");
        if (dashPos != string::npos) line = line.substr(0, dashPos);
        line = trim(line);
        if (line.empty()) return;

        line = state_.vars.expandVariables(line);

        Parser parser(state_.db, defs_, state_.diag);
        Stmt stmt = parser.parseLine(line);
        compileStmt(stmt);
    }

    // ── UI Interface Methods ─────────────────────────────────

    void compileLines(const vector<string>& programLines) {
        auto it = programLines.begin();
        while (it != programLines.end()) {
            string line = *it; ++it;
            size_t dashPos = line.find("---");
            if (dashPos != string::npos) line = line.substr(0, dashPos);
            line = trim(line);
            if (line.empty()) continue;

            line = state_.vars.expandVariables(line);
            string low = lower(line);

            if (startsWith(low, "repeat ") || startsWith(low, "loop ")) {
                string countExpr = trim(line.substr(line.find(' ')));
                if (!countExpr.empty() && countExpr.back() == '{') countExpr.pop_back();
                countExpr = trim(countExpr);

                vector<string> bodyLines;
                while (it != programLines.end()) {
                    string bline = *it; ++it;
                    string bs = trim(bline.substr(0, bline.find("---")));
                    if (bs.empty()) continue;
                    if (bs == "}") break;
                    bodyLines.push_back(bs);
                }

                int count = 0;
                try { count = state_.vars.resolveIntOrVar(countExpr); }
                catch (...) { count = (int)evalSimpleExpr(countExpr); }

                for (int r = 0; r < count; ++r) compileLines(bodyLines);
                continue;
            }

            if (startsWith(low, "def ") && endsWith(low, "{")) {
                while (it != programLines.end()) {
                    string bline = *it; ++it;
                    string bs = trim(bline.substr(0, bline.find("---")));
                    if (bs == "}") break;
                }
                continue;
            }

            if (startsWith(low, "func ")) {
                while (it != programLines.end()) {
                    string bline = *it; ++it;
                    string bs = trim(bline.substr(0, bline.find("---")));
                    if (bs == "}") break;
                }
                continue;
            }

            processLine(line);
        }
    }

    void finish() {
        // Basic finalization before address resolution
    }

    void resolveAdrOf(int homeVal) {
        state_.home = homeVal;

        for (auto& fix : state_.adrOfFixups) {
            auto it = state_.labels.find(fix.label);
            if (it == state_.labels.end())
                throw runtime_error("Undefined label in adr_of: " + fix.label);
            
            int offset = 0;
            if (!fix.offsetExpr.empty() && trim(fix.offsetExpr) != "0") {
                try { offset = (int)evalSimpleExpr(fix.offsetExpr); }
                catch (...) { offset = resolveOffset(fix.offsetExpr); }
            }
            
            if (!fix.baseAddrExpr.empty()) {
                int base_offset = 0;
                try { base_offset = (int)evalSimpleExpr(fix.baseAddrExpr); }
                catch (...) { base_offset = state_.vars.resolveIntOrVar(fix.baseAddrExpr); }
                offset += (base_offset - homeVal);
            }
            
            int targetAdr = homeVal + it->second + offset;
            state_.result[fix.pos] = targetAdr & 0xFF;
            state_.result[fix.pos + 1] = (targetAdr >> 8) & 0xFF;
        }

        for (auto& fix : state_.adrArithFixups) {
            auto lit = state_.labels.find(fix.leftLabel);
            auto rit = state_.labels.find(fix.rightLabel);
            if (lit == state_.labels.end()) throw runtime_error("Undefined label: " + fix.leftLabel);
            if (rit == state_.labels.end()) throw runtime_error("Undefined label: " + fix.rightLabel);
            int loff = resolveOffset(fix.leftOffsetExpr);
            int roff = resolveOffset(fix.rightOffsetExpr);
            int leftAdr = homeVal + lit->second + loff;
            int rightAdr = homeVal + rit->second + roff;
            state_.result[fix.pos] = (rightAdr - leftAdr) & 0xFF;
        }

        for (auto& fix : state_.prLengthFixups) {
            int val = (int)state_.result.size() - fix.pos;
            state_.result[fix.pos] = val & 0xFF;
            state_.result[fix.pos + 1] = (val >> 8) & 0xFF;
        }

        for (auto& fix : state_.remainingLengthFixups) {
            int remaining = (int)state_.result.size() - fix.pos - 2;
            state_.result[fix.pos] = remaining & 0xFF;
            state_.result[fix.pos + 1] = (remaining >> 8) & 0xFF;
        }

        regex adrRegex(R"(adr\(\s*([^,)]+)\s*(?:,\s*([^,)]+)\s*)?(?:,\s*([^)]+)\s*)?\))");
        for (auto& fix : state_.deferredEvalFixups) {
            string expr = fix.expr;
            expr = regexReplaceLambda(expr, adrRegex, [&](const smatch& m) -> string {
                string label = lower(trim(m[1].str()));
                auto it = state_.labels.find(label);
                if (it == state_.labels.end())
                    throw runtime_error("Undefined label in adr(): " + label);
                int offset = 0;
                if (m.size() > 2 && m[2].matched) {
                    string offStr = trim(m[2].str());
                    if (!offStr.empty()) {
                        try { offset = (int)evalSimpleExpr(offStr); }
                        catch (...) { offset = resolveOffset(offStr); }
                    }
                }
                if (m.size() > 3 && m[3].matched) {
                    string baseStr = trim(m[3].str());
                    if (!baseStr.empty()) {
                        int base_offset = 0;
                        try { base_offset = (int)evalSimpleExpr(baseStr); }
                        catch (...) { base_offset = state_.vars.resolveIntOrVar(baseStr); }
                        offset += (base_offset - homeVal);
                    }
                }
                return to_string(homeVal + it->second + offset);
            });

            expr = resolveAdrOfInExpr(expr);

            try {
                long long val = evalSimpleExpr(expr);
                state_.result[fix.pos] = (int)(val & 0xFF);
                state_.result[fix.pos + 1] = (int)((val >> 8) & 0xFF);
            } catch (const exception& e) {
                throw runtime_error(string("Deferred eval error: ") + e.what() + " in '" + fix.expr + "'");
            }
        }
    }
};

} // namespace lc::details

namespace lc {
    using Compiler = details::Compiler;
    using CompilerState = details::CompilerState;
    using CompileOptions = details::CompileOptions;
    using Driver = details::Driver;
    using CommandDatabase = details::CommandDatabase;
    using FontTable = details::FontTable;
    using CalcTable = details::CalcTable;
}