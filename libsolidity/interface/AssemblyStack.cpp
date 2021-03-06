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
/**
 * Full assembly stack that can support EVM-assembly and Yul as input and EVM, EVM1.5 and
 * eWasm as output.
 */


#include <libsolidity/interface/AssemblyStack.h>

#include <liblangutil/Scanner.h>
#include <libyul/AsmPrinter.h>
#include <libyul/AsmParser.h>
#include <libyul/AsmAnalysis.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/AsmCodeGen.h>
#include <libyul/backends/evm/EVMCodeTransform.h>
#include <libyul/backends/evm/EVMAssembly.h>

#include <libevmasm/Assembly.h>

using namespace std;
using namespace dev;
using namespace langutil;
using namespace dev::solidity;

namespace
{
yul::AsmFlavour languageToAsmFlavour(AssemblyStack::Language _language)
{
	switch (_language)
	{
	case AssemblyStack::Language::Assembly:
		return yul::AsmFlavour::Loose;
	case AssemblyStack::Language::StrictAssembly:
		return yul::AsmFlavour::Strict;
	case AssemblyStack::Language::Yul:
		return yul::AsmFlavour::Yul;
	}
	solAssert(false, "");
	return yul::AsmFlavour::Yul;
}

}


Scanner const& AssemblyStack::scanner() const
{
	solAssert(m_scanner, "");
	return *m_scanner;
}

bool AssemblyStack::parseAndAnalyze(std::string const& _sourceName, std::string const& _source)
{
	m_errors.clear();
	m_analysisSuccessful = false;
	m_scanner = make_shared<Scanner>(CharStream(_source), _sourceName);
	m_parserResult = yul::Parser(m_errorReporter, languageToAsmFlavour(m_language)).parse(m_scanner, false);
	if (!m_errorReporter.errors().empty())
		return false;
	solAssert(m_parserResult, "");

	return analyzeParsed();
}

bool AssemblyStack::analyze(yul::Block const& _block, Scanner const* _scanner)
{
	m_errors.clear();
	m_analysisSuccessful = false;
	if (_scanner)
		m_scanner = make_shared<Scanner>(*_scanner);
	m_parserResult = make_shared<yul::Block>(_block);

	return analyzeParsed();
}

bool AssemblyStack::analyzeParsed()
{
	m_analysisInfo = make_shared<yul::AsmAnalysisInfo>();
	yul::AsmAnalyzer analyzer(*m_analysisInfo, m_errorReporter, m_evmVersion, boost::none, languageToAsmFlavour(m_language));
	m_analysisSuccessful = analyzer.analyze(*m_parserResult);
	return m_analysisSuccessful;
}

MachineAssemblyObject AssemblyStack::assemble(Machine _machine) const
{
	solAssert(m_analysisSuccessful, "");
	solAssert(m_parserResult, "");
	solAssert(m_analysisInfo, "");

	switch (_machine)
	{
	case Machine::EVM:
	{
		MachineAssemblyObject object;
		eth::Assembly assembly;
		yul::CodeGenerator::assemble(*m_parserResult, *m_analysisInfo, assembly);
		object.bytecode = make_shared<eth::LinkerObject>(assembly.assemble());
		object.assembly = assembly.assemblyString();
		return object;
	}
	case Machine::EVM15:
	{
		MachineAssemblyObject object;
		yul::EVMAssembly assembly(true);
		yul::CodeTransform(assembly, *m_analysisInfo, m_language == Language::Yul, true)(*m_parserResult);
		object.bytecode = make_shared<eth::LinkerObject>(assembly.finalize());
		/// TODO: fill out text representation
		return object;
	}
	case Machine::eWasm:
		solUnimplemented("eWasm backend is not yet implemented.");
	}
	// unreachable
	return MachineAssemblyObject();
}

string AssemblyStack::print() const
{
	solAssert(m_parserResult, "");
	return yul::AsmPrinter(m_language == Language::Yul)(*m_parserResult);
}
