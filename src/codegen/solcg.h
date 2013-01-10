////////////////////////////////////////////////////////////////////////////////
//
// wxFormBuilder - A Visual Dialog Editor for wxWidgets.
// Copyright (C) 2005 José Antonio Hurtado
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// Written by
//   José Antonio Hurtado - joseantonio.hurtado@gmail.com
//   Juan Antonio Ortega  - jortegalalmolda@gmail.com
//
// SOL code generation writen by
//   Jun-hyeok Jang - ellongrey@gmail.com
//
///////////////////////////////////////////////////////////////////////////////

/**
@file
@author Jun-hyeok Jang - ellongrey@gmail.com
@note
The implementation of the generation of relative paths is a little hacky, and not a solution.
The value of all properties that are file or a directory paths must be absolute, otherwise the code generation will not work.
*/

#ifndef _SOL_CODE_GEN_
#define _SOL_CODE_GEN_

// wxFormBuilder flags (fbfXXX)
#define fbfSILENT true
#define fbfMESSAGE false

#include <set>
#include "codegen.h"
#include <wx/string.h>

class Dict
{
public:
	Dict();
	~Dict();

public:
	bool								Add(const wxString& word, int index);

	struct MatchResult
	{
		int			MatchIndex;
		size_t		Begin;
		size_t		End;
	};

	bool								Match(const wxString& str, MatchResult& result);

	bool								Find(const wxString& str, MatchResult& result)	{ return Find(str, result, 0, str.length()); }
	bool								Find(const wxString& str, MatchResult& result, size_t begin) { return Find(str, result, begin, str.length()); }

	bool								Find(const wxString& str, MatchResult& result, size_t begin, size_t end);

private:
	class DictNode;
	DictNode*			m_First;
};

class PrefixDict
{
public:
	void								AddConverted(const wxString& str, const wxString& converted);
	void								AddPrefix(const wxString& prefix, const wxString& converted);
	wxString							Convert(const wxString& str);

private:
	Dict								m_Dict;

	typedef std::map<wxString, wxString>ConvertedMap;
	ConvertedMap						m_Converted;
	wxArrayString						m_Prefixes;
};

/**
* Parse the SOL templates.
*/
class SolTemplateParser : public TemplateParser
{
private:
	bool m_i18n;
	bool m_useRelativePath;
	wxString m_basePath;

	PrefixDict& m_PrefixDict;
	
	void SetupModulePrefixes();

public:
	SolTemplateParser( PObjectBase obj, wxString _template, bool useI18N, bool useRelativePath, wxString basePath, PrefixDict& prefixDict );
	SolTemplateParser( const SolTemplateParser & that, wxString _template, PrefixDict& prefixDict );

	// overrides for SOL
	PTemplateParser CreateParser( const TemplateParser* oldparser, wxString _template );
	wxString RootWxParentToCode();
	wxString ValueToCode( PObjectBase obj, PropertyType type, wxString value);
	wxString TouchName(PObjectBase obj, const wxString& name);

};

/**
* Generate the SOL code
*/
class SolCodeGenerator : public CodeGenerator
{
private:
	PCodeWriter m_source;

	bool m_useRelativePath;
	bool m_i18n;
	wxString m_basePath;
	unsigned int m_firstID;
	PrefixDict m_PrefixDict;

	/**
	* Predefined macros won't generate defines.
	*/
	std::set<wxString> m_predMacros;

	void SetupPredefinedMacros();

	/**
	* Given an object and the name for a template, obtains the code.
	*/
	wxString GetCode( PObjectBase obj, wxString name, bool silent = false);

	/**
	* Stores the project's objects classes set, for generating the includes.
	*/
	void FindDependencies( PObjectBase obj, std::set< PObjectInfo >& info_set );

	/**
	* Stores all the properties for "macro" type objects, so that their
	* related '#define' can be generated subsequently.
	*/
	void FindMacros( PObjectBase obj, std::vector< wxString >* macros );

	/**
	 * Looks for "non-null" event handlers (PEvent) and collects it into a vector.
	 */
	void FindEventHandlers(PObjectBase obj, EventVector &events);

	/**
	* Generates classes declarations inside the header file.
	*/
	void GenClassDeclaration( PObjectBase class_obj, bool use_enum, const wxString& classDecoration, const EventVector &events, const wxString& eventHandlerPostfix );

	/**
	* Generates the event table.
	*/
	void GenEvents( PObjectBase class_obj, const EventVector &events);

	/**
	* helper function to find the event table entry template in the class or its base classes
	*/
	bool GenEventEntry( PObjectBase obj, PObjectInfo obj_info, const wxString& templateName, const wxString& handlerName);

	/**
	* Generates the generated_event_handlers template
	*/
	void GetGenEventHandlers( PObjectBase obj );
	/**
	* Generates the generated_event_handlers template
	*/
	void GenDefinedEventHandlers( PObjectInfo info, PObjectBase obj );

	/**
	* Generates the 'import' section for files.
	*/
	void GenIncludes( PObjectBase project, std::vector< wxString >* includes, std::set< wxString >* templates );
	void GenObjectIncludes( PObjectBase project, std::vector< wxString >* includes, std::set< wxString >* templates );
	void GenBaseIncludes( PObjectInfo info, PObjectBase obj, std::vector< wxString >* includes, std::set< wxString >* templates );
	void AddUniqueIncludes( const wxString& include, std::vector< wxString >* includes );

	/**
	* Generate a set of all subclasses to forward declare in the generated file.
	*/
	void GenSubclassSets( PObjectBase obj, std::set< wxString >* subclasses, std::vector< wxString >* headerIncludes );

	/**
	* Generates the '#define' section for macros.
	*/
	void GenDefines( PObjectBase project);

	/**
	* Generates the constructor for a class
	*/
	void GenConstructor( PObjectBase class_obj, const EventVector &events );

	/**
	* Generates the destructor for a class
	*/
	void GenDestructor( PObjectBase class_obj, const EventVector &events );

	/**
	* Makes the objects construction, setting up the objects' and Layout properties.
	* The algorithm is simmilar to that used in the designer preview generation.
	*/
	void GenConstruction( PObjectBase obj, bool is_widget );
	
	/**
	* Makes the objects destructions.
	*/
	void GenDestruction( PObjectBase obj);

	/**
	* Configures the object properties, both own and inherited ones.
	* Information for the class is given, because it will recursively make the
	* configuration in the "super-classes".
	*/
	void GenSettings( PObjectInfo info, PObjectBase obj);

	/**
	* Adds a control for a toolbar. Needs the objectinfo (wxWindow type) where
	* the template is found, and the objectbase for the control.
	*/
	void GenAddToolbar( PObjectInfo info, PObjectBase obj );
	void GetAddToolbarCode( PObjectInfo info, PObjectBase obj, wxArrayString& codelines );

    void GenVirtualEventHandlers( const EventVector &events, const wxString& eventHandlerPostfix );

public:
	/**
	* Convert a wxString to the "C/C++" format.
	*/
	static wxString ConvertSolString( wxString text);

	SolCodeGenerator();

	/**
	* Set the codewriter for the source file
	*/
	void SetSourceWriter( PCodeWriter cw )
	{
		m_source = cw;
	}


	/**
	* Configures the reference path for generating relative paths to
	* that passed as parameter.
	*
	* @note path is generated with the separators, '/', since on Windows
	*		the compilers interpret path correctly.
	*/
	void UseRelativePath( bool relative = false, wxString basePath = wxString() );

	/**
	* Set the First ID used during Code Generation.
	*/
	void SetFirstID( const unsigned int id ){ m_firstID = id; }

	/**
	* Generate the project's code
	*/
	bool GenerateCode( PObjectBase project );

	/**
	* Generate an inherited class
	*/
	void GenerateInheritedClass( PObjectBase userClasses, PObjectBase form );

	void GenMemberDeclarations(PObjectBase obj);
};


#endif //_SOL_CODE_GEN_
