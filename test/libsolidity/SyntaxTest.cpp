/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <test/libsolidity/SyntaxTest.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/throw_exception.hpp>
#include <cctype>
#include <fstream>
#include <memory>
#include <stdexcept>

using namespace dev;
using namespace solidity;
using namespace dev::solidity::test;
using namespace dev::solidity::test::formatting;
using namespace std;
namespace fs = boost::filesystem;
using namespace boost::unit_test;

template<typename IteratorType>
void skipWhitespace(IteratorType& it, IteratorType end)
{
	while (it != end && isspace(*it))
		++it;
}

template<typename IteratorType>
void skipSlashes(IteratorType& it, IteratorType end)
{
	while (it != end && *it == '/')
		++it;
}

SyntaxTest::SyntaxTest(string const& _filename)
{
	ifstream file(_filename);
	if (!file)
		BOOST_THROW_EXCEPTION(runtime_error("Cannot open test contract: \"" + _filename + "\"."));
	file.exceptions(ios::badbit);

	m_source = parseSource(file);
	m_expectations = parseExpectations(file);
}

bool SyntaxTest::run(ostream& _stream, string const& _linePrefix, bool const _formatted)
{
	m_errorList = parseAnalyseAndReturnError(m_source, true, true, true).second;
	if (!matchesExpectations(m_errorList))
	{
		std::string nextIndentLevel = _linePrefix + "  ";
		FormattedScope(_stream, _formatted, {BOLD, CYAN}) << _linePrefix << "Expected result:" << endl;
		printExpected(_stream, nextIndentLevel, _formatted);
		FormattedScope(_stream, _formatted, {BOLD, CYAN}) << _linePrefix << "Obtained result:\n";
		printErrorList(_stream, m_errorList, nextIndentLevel, false, false, _formatted);
		return false;
	}
	return true;
}

void SyntaxTest::printExpected(ostream& _stream, string const& _linePrefix, bool const _formatted) const
{
	if (m_expectations.empty())
		FormattedScope(_stream, _formatted, {BOLD, GREEN}) << _linePrefix << "Success" << endl;
	else
		for (auto const& expectation: m_expectations)
		{
			FormattedScope(_stream, _formatted, {BOLD, expectation.type == "Warning" ? YELLOW : RED}) <<
				_linePrefix << expectation.type << ": ";
			_stream << expectation.message << endl;
		}
}

void SyntaxTest::printErrorList(
	ostream& _stream,
	ErrorList const& _errorList,
	string const& _linePrefix,
	bool const _ignoreWarnings,
	bool const _lineNumbers,
	bool const _formatted
) const
{
	if (_errorList.empty())
		FormattedScope(_stream, _formatted, {BOLD, GREEN}) << _linePrefix << "Success" << endl;
	else
		for (auto const& error: _errorList)
		{
			bool isWarning = (error->type() == Error::Type::Warning);
			if (isWarning && _ignoreWarnings) continue;

			{
				FormattedScope scope(_stream, _formatted, {BOLD, isWarning ? YELLOW : RED});
				_stream << _linePrefix;
				if (_lineNumbers)
				{
					int line = offsetToLineNumber(
						boost::get_error_info<errinfo_sourceLocation>(*error)->start
					);
					if (line >= 0)
						_stream << "(" << line << "): ";
				}
				_stream << error->typeName() << ": ";
			}
			_stream << errorMessage(*error) << endl;
		}
}

int SyntaxTest::offsetToLineNumber(int _location) const
{
	// parseAnalyseAndReturnError(...) prepends a version pragma
	_location -= strlen("pragma solidity >=0.0;\n");
	if (_location < 0 || static_cast<size_t>(_location) >= m_source.size())
		return -1;
	else
		return 1 + std::count(m_source.begin(), m_source.begin() + _location, '\n');
}

bool SyntaxTest::matchesExpectations(ErrorList const& _errorList) const
{
	if (_errorList.size() != m_expectations.size())
		return false;
	else
		for (size_t i = 0; i < _errorList.size(); i++)
			if (
				(_errorList[i]->typeName() != m_expectations[i].type) ||
				(errorMessage(*_errorList[i]) != m_expectations[i].message)
			)
				return false;
	return true;
}

string SyntaxTest::errorMessage(Error const& _e)
{
	if (_e.comment())
		return boost::replace_all_copy(*_e.comment(), "\n", "\\n");
	else
		return "NONE";
}

string SyntaxTest::parseSource(istream& _stream)
{
	string source;
	string line;
	string const delimiter("// ----");
	while (getline(_stream, line))
		if (boost::algorithm::starts_with(line, delimiter))
			break;
		else
			source += line + "\n";
	return source;
}

vector<SyntaxTestExpectation> SyntaxTest::parseExpectations(istream& _stream)
{
	vector<SyntaxTestExpectation> expectations;
	string line;
	while (getline(_stream, line))
	{
		auto it = line.begin();

		skipSlashes(it, line.end());
		skipWhitespace(it, line.end());

		if (it == line.end()) continue;

		auto typeBegin = it;
		while (it != line.end() && *it != ':')
			++it;
		string errorType(typeBegin, it);

		// skip colon
		if (it != line.end()) it++;

		skipWhitespace(it, line.end());

		string errorMessage(it, line.end());
		expectations.emplace_back(SyntaxTestExpectation{move(errorType), move(errorMessage)});
	}
	return expectations;
}

#if BOOST_VERSION < 105900
test_case *make_test_case(
	function<void()> const& _fn,
	string const& _name,
	string const& /* _filename */,
	size_t /* _line */
)
{
	return make_test_case(_fn, _name);
}
#endif

int SyntaxTest::registerTests(
	boost::unit_test::test_suite& _suite,
	boost::filesystem::path const& _basepath,
	boost::filesystem::path const& _path
)
{
	int numTestsAdded = 0;
	fs::path fullpath = _basepath / _path;
	if (fs::is_directory(fullpath))
	{
		test_suite* sub_suite = BOOST_TEST_SUITE(_path.filename().string());
		for (auto const& entry: boost::iterator_range<fs::directory_iterator>(
			fs::directory_iterator(fullpath),
			fs::directory_iterator()
		))
			numTestsAdded += registerTests(*sub_suite, _basepath, _path / entry.path().filename());
		_suite.add(sub_suite);
	}
	else
	{
		static vector<unique_ptr<string>> filenames;

		filenames.emplace_back(new string(_path.string()));
		_suite.add(make_test_case(
			[fullpath]
			{
				std::stringstream errorStream;
				if (!SyntaxTest(fullpath.string()).run(errorStream))
					BOOST_ERROR("Test expectation mismatch.\n" + errorStream.str());
			},
			_path.stem().string(),
			*filenames.back(),
			0
		));
		numTestsAdded = 1;
	}
	return numTestsAdded;
}
