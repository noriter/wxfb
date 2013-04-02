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
// NIT code generation writen by
//   Jun-hyeok Jang - ellongrey@gmail.com
//
///////////////////////////////////////////////////////////////////////////////

#include "nitcg.h"
#include "codewriter.h"
#include "utils/typeconv.h"
#include "utils/debug.h"
#include "rad/appdata.h"
#include "model/objectbase.h"
#include "model/database.h"
#include "utils/wxfbexception.h"

#include <algorithm>

#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/defs.h>

class Dict::DictNode
{
public:
	DictNode() : m_MatchIndex(-1)
	{
	}

	~DictNode()
	{
		for (NodeMap::iterator itr = m_NextNodes.begin(), end = m_NextNodes.end(); itr != end; ++itr)
			delete itr->second;
	}

	typedef std::map<int, DictNode*> NodeMap;

	NodeMap				m_NextNodes;
	int					m_MatchIndex;
};

Dict::Dict()
{
	m_First = new DictNode();
}

Dict::~Dict()
{
	delete m_First;
}

bool Dict::Add(const wxString& word, int index)
{
	DictNode* n = m_First;

	for (size_t i = 0; i < word.length(); ++i)
	{
		int ch = word[i];
		DictNode::NodeMap::iterator itr = n->m_NextNodes.find(ch);
		if (itr != n->m_NextNodes.end())
		{
			n = itr->second;
			continue;
		}

		DictNode* nn = new DictNode();
		n->m_NextNodes.insert(std::make_pair(ch, nn));
		n = nn;
	}

	if (n->m_MatchIndex != -1 && n->m_MatchIndex != index)
		return false;

	n->m_MatchIndex = index;
	return true;
}

bool Dict::Match(const wxString& str, MatchResult& result)
{
	DictNode* match = NULL;
	DictNode* n = m_First;

	for (size_t i = 0; i < str.length(); ++i)
	{
		int ch = str[i];

		DictNode::NodeMap::iterator itr = n->m_NextNodes.find(ch);
		if (itr == n->m_NextNodes.end())
			break;

		n = itr->second;

		if (n->m_MatchIndex != -1)
		{
			result.End = i+1;
			match = n;
		}
	}

	if (match && match->m_MatchIndex != -1)
	{
		result.MatchIndex = match->m_MatchIndex;
		result.Begin = 0;
		return true;
	}
	else
	{
		result.MatchIndex = -1;
		return false;
	}
}

bool Dict::Find(const wxString& str, MatchResult& result, size_t begin, size_t end)
{
	for (size_t p = begin; p < end; ++p)
	{
		DictNode* n = m_First;
		DictNode* match = NULL;

		for (size_t i = p; i < end; ++i)
		{
			int ch = str[i];

			DictNode::NodeMap::iterator itr = n->m_NextNodes.find(ch);
			if (itr == n->m_NextNodes.end())
				break;

			n = itr->second;

			if (n->m_MatchIndex != -1)
			{
				match = n;
				result.End = i+1;
			}
		}

		if (match)
		{
			result.MatchIndex = match->m_MatchIndex;
			result.Begin = p;
			return true;
		}
	}

	result.MatchIndex = -1;
	return false;
}

void PrefixDict::AddConverted(const wxString& str, const wxString& converted)
{
	m_Converted.insert(std::make_pair(str, converted));
}

void PrefixDict::AddPrefix(const wxString& prefix, const wxString& converted)
{
	m_Dict.Add(prefix, m_Prefixes.size());
	m_Prefixes.push_back(converted);
}

wxString PrefixDict::Convert(const wxString& str)
{
	ConvertedMap::iterator itr = m_Converted.find(str);

	if (itr != m_Converted.end())
		return itr->second;

	Dict::MatchResult r;

	if (m_Dict.Match(str, r))
	{
		wxString converted = m_Prefixes[r.MatchIndex] + str.substr(r.End);
		m_Converted.insert(std::make_pair(str, converted));
		return converted;
	}

	return str;
}

NitTemplateParser::NitTemplateParser( PObjectBase obj, wxString _template, bool useI18N, bool useRelativePath, wxString basePath, PrefixDict& prefixDict )
:
TemplateParser(obj,_template),
m_i18n( useI18N ),
m_useRelativePath( useRelativePath ),
m_basePath( basePath ),
m_PrefixDict(prefixDict)
{
	if ( !wxFileName::DirExists( m_basePath ) )
	{
		m_basePath.clear();
	}

	SetupModulePrefixes();
}

NitTemplateParser::NitTemplateParser( const NitTemplateParser & that, wxString _template, PrefixDict& prefixDict  )
:
TemplateParser( that, _template ),
m_i18n( that.m_i18n ),
m_useRelativePath( that.m_useRelativePath ),
m_basePath( that.m_basePath ),
m_PrefixDict(that.m_PrefixDict)
{
	SetupModulePrefixes();
}

wxString NitTemplateParser::RootWxParentToCode()
{
	return wxT("this");
}

PTemplateParser NitTemplateParser::CreateParser( const TemplateParser* oldparser, wxString _template )
{
	const NitTemplateParser* nitOldParser = dynamic_cast< const NitTemplateParser* >( oldparser );
	if ( nitOldParser != NULL )
	{
		PTemplateParser newparser( new NitTemplateParser( *nitOldParser, _template, m_PrefixDict ) );
		return newparser;
	}
	return PTemplateParser();
}

/**
* Convert the value of the property to NIT code
*/
wxString NitTemplateParser::ValueToCode( PObjectBase obj, PropertyType type, wxString value )
{
	wxString result;

	switch ( type )
	{
	case PT_WXPARENT:
		{
			result = value;
			break;
		}
	case PT_WXSTRING:
	case PT_FILE:
	case PT_PATH:
		{
			if ( value.empty() )
			{
				result << wxT("\"\"");
			}
			else
			{
				result << wxT("\"") << NitCodeGenerator::ConvertNitString( value ) << wxT("\"");
			}
			break;
		}
	case PT_WXSTRING_I18N:
		{
			if ( value.empty() )
			{
				result << wxT("\"\"");
			}
			else
			{
				if ( m_i18n )
				{
					result << wxT("_T(\"") << NitCodeGenerator::ConvertNitString(value) << wxT("\")");
				}
				else
				{
					result << wxT("\"") << NitCodeGenerator::ConvertNitString(value) << wxT("\"");
				}
			}
			break;
		}
	case PT_CLASS:
	case PT_MACRO:
	case PT_OPTION:
		{
			result = m_PrefixDict.Convert(value);
			break;
		}
	case PT_TEXT:
	case PT_FLOAT:
	case PT_INT:
	case PT_UINT:
		{
			result = value;
			break;
		}
	case PT_BITLIST:
		{
			if (value.empty())
				result = wxT("0");
			else
			{
				wxString pred, bit;
				wxStringTokenizer bits( value, wxT("|"), wxTOKEN_STRTOK );

				while( bits.HasMoreTokens() )
				{
					bit = bits.GetNextToken();
					bit.Trim(true).Trim(false);
					wxString converted = m_PrefixDict.Convert(bit);
					result += converted;
					if (bits.HasMoreTokens())
						result += " | ";
				}
			}
			break;
		}
	case PT_WXPOINT:
		{
			if ( value.empty() )
			{
				result = wxT("wx.DEFAULT.POS");
			}
			else
			{
				result << wxT("wx.Point(") << value << wxT(")");
			}
			break;
		}
	case PT_WXSIZE:
		{
			if ( value.empty() )
			{
				result = wxT("wx.DEFAULT.SIZE");
			}
			else
			{
				result << wxT("wx.Size(") << value << wxT(")");
			}
			break;
		}
	case PT_BOOL:
		{
			result = ( value == wxT("0") ? wxT("false") : wxT("true") );
			break;
		}
	case PT_WXFONT:
		{
			if ( !value.empty() )
			{
				wxFontContainer font = TypeConv::StringToFont( value );

				int pointSize = font.GetPointSize();
				wxString size = pointSize <= 0 ?
#if wxVERSION_NUMBER < 2900
					wxT("wx.NORMAL_FONT.GetPointSize()")
					: wxString::Format( wxT("%i"), pointSize ).c_str();

				result = wxString::Format
					(
					wxT("wx.Font( %s, %i, %i, %i, %s, %s )"),
					size.c_str(), font.GetFamily(), font.GetStyle(), font.GetWeight(),
					( font.GetUnderlined() ? wxT("True") : wxT("False") ),
					( font.m_faceName.empty() ? wxT("wx.EmptyString")
					: wxString::Format( wxT("\"%s\""), font.m_faceName.c_str() ).c_str() )
#else
					"wx.FONT.NORMAL.PointSize"
					: wxString::Format( "%i", pointSize );

				wxString family;
				wxString style;
				wxString weight;

				switch (font.GetFamily())
				{
				case wxFONTFAMILY_DEFAULT:		family = "wx.Font.FAMILY.DEFAULT"; break;
				case wxFONTFAMILY_DECORATIVE:	family = "wx.Font.FAMILY.DECORATIVE"; break;
				case wxFONTFAMILY_ROMAN:		family = "wx.Font.FAMILY.ROMAN"; break;
				case wxFONTFAMILY_SCRIPT:		family = "wx.Font.FAMILY.SCRIPT"; break;
				case wxFONTFAMILY_SWISS:		family = "wx.Font.FAMILY.SWISS"; break;
				case wxFONTFAMILY_MODERN:		family = "wx.Font.FAMILY.MODERN"; break;
				case wxFONTFAMILY_TELETYPE:		family = "wx.Font.FAMILY.TELETYPE"; break;
				default:						family = wxString::Format("%i", font.GetFamily());
				}

				switch (font.GetStyle())
				{
				case wxFONTSTYLE_NORMAL:		style = "wx.Font.STYLE.NORMAL"; break;
				case wxFONTSTYLE_ITALIC:		style = "wx.Font.STYLE.ITALIC"; break;
				case wxFONTSTYLE_SLANT:			style = "wx.Font.STYLE.SLANT"; break;
				default:						style = wxString::Format("%i", font.GetStyle()); 
				}

				switch (font.GetWeight())
				{
				case wxFONTWEIGHT_NORMAL:		weight = "wx.Font.WEIGHT.NORMAL"; break;
				case wxFONTWEIGHT_LIGHT:		weight = "wx.Font.WEIGHT.LIGHT"; break;
				case wxFONTWEIGHT_BOLD:			weight = "wx.Font.WEIGHT.BOLD"; break;
				default:						weight = wxString::Format("%i", font.GetWeight()); 
				}

				result = wxString::Format
					(
					"wx.Font(%s, %s, %s, %s, %s, %s)",
					size, family, style, weight,
					( font.GetUnderlined() ? "true" : "false" ),
					( font.m_faceName.empty() ? "\"\""
					: wxString::Format( "\"%s\"", font.m_faceName ) )
#endif
					);
			}
			else
			{
				result = wxT("wx.FONT.NORMAL");
			}
			break;
		}
	case PT_WXCOLOUR:
		{
			if ( !value.empty() )
			{
				if ( value.find_first_of( wxT("wx") ) == 0 )
				{
					result = m_PrefixDict.Convert(value);
				}
				else
				{
					wxColour colour = TypeConv::StringToColour( value );
					result = wxString::Format(wxT("0x%02X%02X%02X"), colour.Red(), colour.Green(), colour.Blue());
				}
			}
			else
			{
				result = wxT("null");
			}
			break;
		}
	case PT_BITMAP:
		{
			wxString path;
			wxString source;
			wxSize icoSize;
			TypeConv::ParseBitmapWithResource( value, &path, &source, &icoSize );

			if ( path.empty() )
			{
				// Empty path, generate Null Bitmap
				result = wxT("null");
				break;
			}

			if ( path.StartsWith( wxT("file:") ) )
			{
				wxLogWarning( wxT("NIT code generation does not support using URLs for bitmap properties:\n%s"), path.c_str() );
				result = wxT("null");
				break;
			}

			if ( source == _("Package") || source == _("File"))
			{
				wxString absPath;
				try
				{
					absPath = TypeConv::MakeAbsolutePath( path, AppData()->GetProjectPath() );
				}
				catch( wxFBException& ex )
				{
					wxLogError( ex.what() );
					result = wxT( "null" );
					break;
				}

				wxString file = ( m_useRelativePath ? TypeConv::MakeRelativePath( absPath, m_basePath ) : absPath );

				if (source == _("Package"))
				{
					result << wxT("wx.Bitmap(pack.Locate(\"") << NitCodeGenerator::ConvertNitString( file ) << wxT("\"))");
				}
				else
				{
					result << wxT("wx.Bitmap(\"") << NitCodeGenerator::ConvertNitString( file ) << wxT("\", wx.Bitmap.TYPE.ANY)");
				}
			}
			else if ( source == _("Resource") )
			{
				result << wxT("wx.Bitmap(\"") << path << wxT("\", wx.Bitmap.TYPE.RESOURCE )");
			}
			else if ( source == _("Icon Resource") )
			{
				if ( wxDefaultSize == icoSize )
				{
					result << wxT("wx.Icon(") << path << wxT(")");
				}
				else
				{
					result.Printf( wxT("wx.Icon(\"%s\", wx.Bitmap.TYPE._ICO_RESOURCE, %i, %i)"), path.c_str(), icoSize.GetWidth(), icoSize.GetHeight() );
				}
			}
			else if ( source == _("Art Provider") )
			{
				wxString rid = path.BeforeFirst( wxT(':') );

				if( rid.StartsWith( wxT("gtk-") ) )
				{
					rid = wxT("\"") + rid + wxT("\"");
				}
				else
					rid = m_PrefixDict.Convert(rid);
					
				wxString cid = path.AfterFirst( wxT(':') );
				cid = m_PrefixDict.Convert(cid);

				result = wxT("wx.ArtProvider.GetBitmap(") + rid + wxT(", ") +  cid + wxT(")");
			}
			break;
		}
	case PT_STRINGLIST:
		{
			// Stringlists are generated like a sequence of wxString separated by ', '.
			wxArrayString array = TypeConv::StringToArrayString( value );
			if ( array.Count() > 0 )
			{
				result = ValueToCode( obj, PT_WXSTRING_I18N, array[0] );
			}

			for ( size_t i = 1; i < array.Count(); i++ )
			{
				result << wxT(", ") << ValueToCode( obj, PT_WXSTRING_I18N, array[i] );
			}
			break;
		}
	default:
		break;
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////////

NitCodeGenerator::NitCodeGenerator()
{
	SetupPredefinedMacros();
	m_useRelativePath = false;
	m_i18n = false;
	m_firstID = 1000;
}

wxString NitCodeGenerator::ConvertNitString( wxString text )
{
	wxString result;

	for ( size_t i = 0; i < text.length(); i++ )
	{
		wxChar c = text[i];

		switch ( c )
		{
		case wxT('"'):
			result += wxT("\\\"");
			break;

		case wxT('\\'):
			result += wxT("\\\\");
			break;

		case wxT('\t'):
			result += wxT("\\t");
			break;

		case wxT('\n'):
			result += wxT("\\n");
			break;

		case wxT('\r'):
			result += wxT("\\r");
			break;

		default:
			result += c;
			break;
		}
	}
	return result;
}

void NitCodeGenerator::GenerateInheritedClass( PObjectBase userClasses, PObjectBase form )
{
	if (!userClasses)
	{
		wxLogError(wxT("There is no object to generate inherited class"));
		return;
	}

	if ( wxT("UserClasses") != userClasses->GetClassName() )
	{
		wxLogError(wxT("This not a UserClasses object"));
		return;
	}

	wxString type = userClasses->GetPropertyAsString( wxT("type") );

	// Start file
	wxString code = GetCode( userClasses, wxT("file_comment") );
	m_source->WriteLn( code );
	m_source->WriteLn( wxEmptyString );

	code = GetCode( userClasses, wxT("source_include") );
	m_source->WriteLn( code );
	m_source->WriteLn( wxEmptyString );

	code = GetCode( userClasses, wxT("class_decl") );
	m_source->WriteLn( code );
	m_source->Indent();

	code = GetCode( userClasses, type + wxT("_cons_def") );
	m_source->WriteLn( code );

	// Do events
	EventVector events;
	FindEventHandlers( form, events );

	if ( events.size() > 0 )
	{
		code = GetCode( userClasses, wxT("event_handler_comment") );
		m_source->WriteLn( code );

		std::set<wxString> generatedHandlers;
		for ( size_t i = 0; i < events.size(); i++ )
		{
			PEvent event = events[i];
			if ( generatedHandlers.find( event->GetValue() ) == generatedHandlers.end() )
			{
				m_source->WriteLn( wxString::Format( wxT("def %s( self, event ):"),  event->GetValue().c_str() ) );
				m_source->Indent();
				m_source->WriteLn( wxString::Format( wxT("# TODO: Implement %s"), event->GetValue().c_str() ) );
				m_source->WriteLn( wxT("pass") );
				m_source->Unindent();
				m_source->WriteLn( wxEmptyString );
				generatedHandlers.insert(event->GetValue());
			}
		}
		m_source->WriteLn( wxEmptyString );
	}

	m_source->Unindent();
}

bool NitCodeGenerator::GenerateCode( PObjectBase project )
{
	if (!project)
	{
		wxLogError(wxT("There is no project to generate code"));
		return false;
	}

	m_i18n = false;
	PProperty i18nProperty = project->GetProperty( wxT("internationalize") );
	if (i18nProperty && i18nProperty->GetValueAsInteger())
		m_i18n = true;

	m_source->Clear();

	wxString code = (
		wxT("// NIT code generated with wxFormBuilder for nit (version ") wxT(__DATE__) wxT(")\n")
		wxT("// PLEASE DO \"NOT\" EDIT THIS FILE!\n") );

	m_source->WriteLn( code );

	// Insert nit preamble

	code = GetCode( project, wxT("nit_preamble") );
	if ( !code.empty() )
	{
		m_source->WriteLn( code );
	}

	PProperty propFile = project->GetProperty( wxT("file") );
	if (!propFile)
	{
		wxLogError( wxT("Missing \"file\" property on Project Object") );
		return false;
	}

	wxString file = propFile->GetValue();
	if ( file.empty() )
	{
		file = wxT("noname");
	}

	// Generate the subclass sets
	std::set< wxString > subclasses;
	std::vector< wxString > headerIncludes;

	GenSubclassSets( project, &subclasses, &headerIncludes );

	// Generating in the .h header file those include from components dependencies.
	std::set< wxString > templates;
	GenIncludes(project, &headerIncludes, &templates );

	// Write the include lines
	std::vector<wxString>::iterator include_it;
	for ( include_it = headerIncludes.begin(); include_it != headerIncludes.end(); ++include_it )
	{
		m_source->WriteLn( *include_it );
	}
	if ( !headerIncludes.empty() )
	{
		m_source->WriteLn();
	}

	// Write internationalization support
	if( m_i18n )
	{
		m_source->WriteLn( wxT("import gettext") );
		m_source->WriteLn( wxT("_ = gettext.gettext") );
		m_source->WriteLn();
	}

	// Generating "defines" for macros
	GenDefines( project );

	wxString eventHandlerPostfix;
	PProperty eventKindProp = project->GetProperty( wxT("skip_nit_events") );
	if( eventKindProp->GetValueAsInteger() )
	{
		eventHandlerPostfix = wxT(" { event.Skip() }");
	}
	else
		eventHandlerPostfix = wxT(" { throw \"not implemented\" }");

	for ( unsigned int i = 0; i < project->GetChildCount(); i++ )
	{
		PObjectBase child = project->GetChild( i );

		EventVector events;
		FindEventHandlers( child, events );
		//GenClassDeclaration( child, useEnum, classDecoration, events, eventHandlerPrefix, eventHandlerPostfix );
		GenClassDeclaration( child, false, wxT(""), events, eventHandlerPostfix );
	}

	code = GetCode( project, wxT("nit_epilogue") );
	if( !code.empty() ) 
	{
		m_source->WriteLn();
		m_source->WriteLn( wxT("////////////////////////////////////////////////////////////////////////////////") );
		m_source->WriteLn();
		m_source->WriteLn( code );
	}

	return true;
}

void NitCodeGenerator::GenEvents( PObjectBase class_obj, const EventVector &events)
{
	if ( events.empty() )
		return;

	m_source->WriteLn();
	m_source->WriteLn( wxT("// Connect Events") );

	PProperty propName = class_obj->GetProperty( wxT("name") );
	if ( !propName )
	{
		wxLogError(wxT("Missing \"name\" property on \"%s\" class. Review your XML object description"),
			class_obj->GetClassName().c_str());
		return;
	}

	wxString class_name = propName->GetValue();
	if ( class_name.empty() )
	{
		wxLogError( wxT("Object name cannot be null") );
		return;
	}

	wxString base_class;
	wxString handlerName;

	PProperty propSubclass = class_obj->GetProperty( wxT("subclass") );
	if ( propSubclass )
	{
		wxString subclass = propSubclass->GetChildFromParent( wxT("name") );
		if ( !subclass.empty() )
		{
			base_class = subclass;
		}
	}

	base_class = m_PrefixDict.Convert(base_class);

	if ( base_class.empty() )
		base_class = wxT("wx.") + class_obj->GetClassName();

	if ( events.size() > 0 )
	{
		for ( size_t i = 0; i < events.size(); i++ )
		{
			PEvent event = events[i];

			handlerName = event->GetValue();

			wxString templateName = wxString::Format( wxT("connect_%s"), event->GetName().c_str() );

			PObjectBase obj = event->GetObject();
			if ( !GenEventEntry( obj, obj->GetObjectInfo(), templateName, handlerName) )
			{
				wxLogError( wxT("Missing \"evt_%s\" template for \"%s\" class. Review your XML object description"),
					templateName.c_str(), class_name.c_str() );
			}
		}
	}
}

bool NitCodeGenerator::GenEventEntry( PObjectBase obj, PObjectInfo obj_info, const wxString& templateName, const wxString& handlerName)
{
	wxString _template;
	PCodeInfo code_info = obj_info->GetCodeInfo( wxT("nit") );
	if ( code_info )
	{
		_template = code_info->GetTemplate( wxString::Format( wxT("evt_%s"), templateName.c_str() ) );

		if ( !_template.empty() )
		{
			if (handlerName.StartsWith("@"))
				_template.Replace(wxT("#handler"), wxT("@") + handlerName); 
			else
				_template.Replace(wxT("#handler"), handlerName); 
			NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
			m_source->WriteLn( parser.ParseTemplate() );
			return true;
		}
	}

	for ( unsigned int i = 0; i < obj_info->GetBaseClassCount(false); i++ )
	{
		PObjectInfo base_info = obj_info->GetBaseClass( i, false );
		if ( GenEventEntry( obj, base_info, templateName, handlerName ) )
		{
			return true;
		}
	}

	return false;
}

void NitCodeGenerator::GenVirtualEventHandlers( const EventVector& events, const wxString& eventHandlerPostfix )
{
	if ( events.size() > 0 )
	{
		// There are problems if we create "pure" virtual handlers, because some
		// events could be triggered in the constructor in which virtual methods are
		// execute properly.
		// So we create a default handler which will skip the event.
		m_source->WriteLn( wxEmptyString );
		m_source->WriteLn( wxT("// Event Handler Declarations, Complete them before instantiate") );

		std::set<wxString> generatedHandlers;
		for ( size_t i = 0; i < events.size(); i++ )
		{
			PEvent event = events[i];
			if (event->GetValue().StartsWith("@"))
				continue;

			wxString evtCls = event->GetEventInfo()->GetEventClassName();
			evtCls = m_PrefixDict.Convert(evtCls);

			wxString aux = wxT("function ") + event->GetValue() + wxT("(event: ") + evtCls + wxT(")");

			if (generatedHandlers.find(aux) == generatedHandlers.end())
			{
				m_source->WriteLn(aux + eventHandlerPostfix);

				generatedHandlers.insert(aux);
			}
		}
	}
}

void NitCodeGenerator::GetGenEventHandlers( PObjectBase obj )
{
	GenDefinedEventHandlers( obj->GetObjectInfo(), obj );

	for (unsigned int i = 0; i < obj->GetChildCount() ; i++)
	{
		PObjectBase child = obj->GetChild(i);
		GetGenEventHandlers(child);
	}
}

void NitCodeGenerator::GenDefinedEventHandlers( PObjectInfo info, PObjectBase obj )
{
	PCodeInfo code_info = info->GetCodeInfo( wxT( "nit" ) );
	if ( code_info )
	{
		wxString _template = code_info->GetTemplate( wxT("generated_event_handlers") );
		if ( !_template.empty() )
		{
			NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
			wxString code = parser.ParseTemplate();

			if ( !code.empty() )
			{
				m_source->WriteLn(code);
			}
		}
	}

	// Proceeding recursively with the base classes
	for ( unsigned int i = 0; i < info->GetBaseClassCount(false); i++ )
	{
		PObjectInfo base_info = info->GetBaseClass( i, false );
		GenDefinedEventHandlers( base_info, obj );
	}
}


wxString NitCodeGenerator::GetCode(PObjectBase obj, wxString name, bool silent)
{
	wxString _template;
	PCodeInfo code_info = obj->GetObjectInfo()->GetCodeInfo( wxT("nit") );

	if (!code_info)
	{
		if( !silent )
		{
			wxString msg( wxString::Format( wxT("Missing \"%s\" template for \"%s\" class. Review your XML object description"),
				name.c_str(), obj->GetClassName().c_str() ) );
			wxLogError(msg);
		}
		return wxT("");
	}

	_template = code_info->GetTemplate(name);

	NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
	wxString code = parser.ParseTemplate();

	return code;
}

void NitCodeGenerator::GenClassDeclaration(PObjectBase class_obj, bool use_enum, const wxString& classDecoration, const EventVector &events, const wxString& eventHandlerPostfix)
{
	PProperty propName = class_obj->GetProperty( wxT("name") );
	if ( !propName )
	{
		wxLogError(wxT("Missing \"name\" property on \"%s\" class. Review your XML object description"),
			class_obj->GetClassName().c_str());
		return;
	}

	wxString class_name = propName->GetValue();
	if ( class_name.empty() )
	{
		wxLogError( wxT("Object name can not be null") );
		return;
	}

	m_source->WriteLn( );
	m_source->WriteLn( wxT("////////////////////////////////////////////////////////////////////////////////") );
	m_source->WriteLn( );
	m_source->WriteLn( wxT("// Class ") + class_name);
	m_source->WriteLn( );

	m_source->WriteLn( wxT("class ") + classDecoration + class_name + wxT(" : ") + GetCode( class_obj, wxT("base") ).Trim() );
	m_source->WriteLn( wxT("{") );
	m_source->Indent();

	// The constructor is also included within public
	GenConstructor( class_obj, events );
	GenDestructor( class_obj, events );

	m_source->WriteLn( wxT("") );

	// event handlers
	GenVirtualEventHandlers(events, eventHandlerPostfix);
	GetGenEventHandlers( class_obj );
	m_source->WriteLn();

	// members
	m_source->WriteLn( wxT("// Member Declarations") );
	GenMemberDeclarations(class_obj);
	m_source->WriteLn();

	m_source->Unindent();
	m_source->WriteLn( wxT("}") );
}

void NitCodeGenerator::GenMemberDeclarations(PObjectBase obj)
{
	wxString typeName = obj->GetObjectTypeName();

	bool expose = obj->GetPropertyAsInteger(wxT("expose_member")) != 0;
	if (expose || typeName == wxT("form") || typeName == wxT("wizard"))
	{
		wxString code = GetCode(obj, wxT("declaration"), true);
		if (!code.empty())
			m_source->WriteLn(code);
	}

	for (unsigned int i = 0; i < obj->GetChildCount(); ++i)
	{
		PObjectBase child = obj->GetChild(i);

		GenMemberDeclarations(child);
	}
}

void NitCodeGenerator::GenSubclassSets( PObjectBase obj, std::set< wxString >* subclasses, std::vector< wxString >* headerIncludes )
{
	// Call GenSubclassForwardDeclarations on all children as well
	for ( unsigned int i = 0; i < obj->GetChildCount(); i++ )
	{
		GenSubclassSets( obj->GetChild( i ), subclasses, headerIncludes );
	}

	// Fill the set
	PProperty subclass = obj->GetProperty( wxT("subclass") );
	if ( subclass )
	{
		std::map< wxString, wxString > children;
		subclass->SplitParentProperty( &children );

		std::map< wxString, wxString >::iterator name;
		name = children.find( wxT("name") );

		if ( children.end() == name )
		{
			// No name, so do nothing
			return;
		}

		wxString nameVal = name->second;
		if ( nameVal.empty() )
		{
			// No name, so do nothing
			return;
		}

		// Now get the header
		std::map< wxString, wxString >::iterator header;
		header = children.find( wxT("header") );

		if ( children.end() == header )
		{
			// No header, so do nothing
			return;
		}

		wxString headerVal = header->second;
		if ( headerVal.empty() )
		{
			// No header, so do nothing
			return;
		}

		// Got a header
		PObjectInfo info = obj->GetObjectInfo();
		if ( !info )
		{
			return;
		}

		PObjectPackage pkg = info->GetPackage();
		if ( !pkg )
		{
			return;
		}

		wxString include = wxT("require \"") + headerVal + wxT("\"");
		std::vector< wxString >::iterator it = std::find( headerIncludes->begin(), headerIncludes->end(), include );
		if ( headerIncludes->end() == it )
		{
			headerIncludes->push_back( include );
		}
	}
}

void NitCodeGenerator::GenIncludes( PObjectBase project, std::vector<wxString>* includes, std::set< wxString >* templates )
{
	GenObjectIncludes( project, includes, templates );
}

void NitCodeGenerator::GenObjectIncludes( PObjectBase project, std::vector< wxString >* includes, std::set< wxString >* templates )
{
	// Fill the set
	PCodeInfo code_info = project->GetObjectInfo()->GetCodeInfo( wxT("nit") );
	if (code_info)
	{
		NitTemplateParser parser( project, code_info->GetTemplate( wxT("include") ), m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
		wxString include = parser.ParseTemplate();
		if ( !include.empty() )
		{
			if ( templates->insert( include ).second )
			{
				AddUniqueIncludes( include, includes );
			}
		}
	}

	// Call GenIncludes on all children as well
	for ( unsigned int i = 0; i < project->GetChildCount(); i++ )
	{
		GenObjectIncludes( project->GetChild(i), includes, templates );
	}

	// Generate includes for base classes
	GenBaseIncludes( project->GetObjectInfo(), project, includes, templates );
}

void NitCodeGenerator::GenBaseIncludes( PObjectInfo info, PObjectBase obj, std::vector< wxString >* includes, std::set< wxString >* templates )
{
	if ( !info )
	{
		return;
	}

	// Process all the base classes recursively
	for ( unsigned int i = 0; i < info->GetBaseClassCount(false); i++ )
	{
		PObjectInfo base_info = info->GetBaseClass( i, false );
		GenBaseIncludes( base_info, obj, includes, templates );
	}

	PCodeInfo code_info = info->GetCodeInfo( wxT("nit") );
	if ( code_info )
	{
		NitTemplateParser parser( obj, code_info->GetTemplate( wxT("include") ), m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
		wxString include = parser.ParseTemplate();
		if ( !include.empty() )
		{
			if ( templates->insert( include ).second )
			{
				AddUniqueIncludes( include, includes );
			}
		}
	}
}

void NitCodeGenerator::AddUniqueIncludes( const wxString& include, std::vector< wxString >* includes )
{
	// Split on newlines to only generate unique include lines
	// This strips blank lines and trims
	wxStringTokenizer tkz( include, wxT("\n"), wxTOKEN_STRTOK );

	while ( tkz.HasMoreTokens() )
	{
		wxString line = tkz.GetNextToken();
		line.Trim( false );
		line.Trim( true );

		// If it is not an include line, it will be written
		if ( !line.StartsWith( wxT("import") ) )
		{
			includes->push_back( line );
			continue;
		}

		// If it is an include, it must be unique to be written
		std::vector< wxString >::iterator it = std::find( includes->begin(), includes->end(), line );
		if ( includes->end() == it )
		{
			includes->push_back( line );
		}
	}
}

void NitCodeGenerator::FindDependencies( PObjectBase obj, std::set< PObjectInfo >& info_set )
{
	unsigned int ch_count = obj->GetChildCount();
	if (ch_count > 0)
	{
		unsigned int i;
		for (i = 0; i<ch_count; i++)
		{
			PObjectBase child = obj->GetChild(i);
			info_set.insert(child->GetObjectInfo());
			FindDependencies(child, info_set);
		}
	}
}

void NitCodeGenerator::GenConstructor( PObjectBase class_obj, const EventVector &events )
{
	// generate function definition
	m_source->WriteLn( GetCode( class_obj, wxT("cons_def") ) );
	m_source->WriteLn( wxT("{") );
	m_source->Indent();

	m_source->WriteLn( GetCode( class_obj, wxT("cons_call") ) );
	m_source->WriteLn();

	wxString settings = GetCode( class_obj, wxT("settings") );
	if ( !settings.IsEmpty() )
	{
		m_source->WriteLn( settings );
	}

	for ( unsigned int i = 0; i < class_obj->GetChildCount(); i++ )
	{
		GenConstruction( class_obj->GetChild( i ), true );
	}

	wxString afterAddChild = GetCode( class_obj, wxT("after_addchild") );
	if ( !afterAddChild.IsEmpty() )
	{
		m_source->WriteLn( afterAddChild );
	}

	GenEvents( class_obj, events );

	wxStringTokenizer st(class_obj->GetPropertyAsString(wxT("init_code")), wxT("\n"), wxTOKEN_RET_EMPTY);

	if (st.HasMoreTokens())
	{
		m_source->WriteLn();
		m_source->WriteLn("// init_code ///////////");
		m_source->WriteLn();

		while (st.HasMoreTokens())
		{
			m_source->WriteLn(st.NextToken());
		}
	}

	m_source->Unindent();
	m_source->WriteLn( wxT("}") );

	if ( class_obj->GetObjectTypeName() == wxT("wizard") && class_obj->GetChildCount() > 0 )
	{
		m_source->WriteLn( );
		m_source->WriteLn( wxT("function AddPage(page: wx.WizardPageSimple)") );
		m_source->WriteLn( wxT("{") );
		m_source->Indent();
		m_source->WriteLn( wxT("if (_Pages.len()) page.Chain(_Pages.top(), page)") );
		m_source->WriteLn( wxT("_Pages.push(page)") );
		m_source->Unindent();
		m_source->WriteLn( wxT("}") );
	}
}

void NitCodeGenerator::GenDestructor( PObjectBase class_obj, const EventVector &events )
{
	m_source->WriteLn();
	// generate function definition
	m_source->WriteLn( wxT("destructor()") );
	m_source->WriteLn( wxT("{") );
	m_source->Indent();

	// destruct objects
	GenDestruction( class_obj );

	m_source->Unindent();
	m_source->WriteLn( wxT("}") );
}

void NitCodeGenerator::GenConstruction(PObjectBase obj, bool is_widget )
{
	wxString type = obj->GetObjectTypeName();
	PObjectInfo info = obj->GetObjectInfo();

	if ( ObjectDatabase::HasNitProperties( type ) )
	{
		bool expose = obj->GetPropertyAsInteger(wxT("expose_member")) != 0;
		bool hasName = !obj->GetPropertyAsString("name").empty();

		m_source->WriteLn( wxString(!hasName || expose ? wxT("") : wxT("var ")) + GetCode( obj, wxT("construction") ) );

		GenSettings( obj->GetObjectInfo(), obj );

		bool isWidget = !info->IsSubclassOf( wxT("sizer") );

		for ( unsigned int i = 0; i < obj->GetChildCount(); i++ )
		{
			PObjectBase child = obj->GetChild( i );
			GenConstruction( child, isWidget );

			if ( type == wxT("toolbar") )
			{
				GenAddToolbar(child->GetObjectInfo(), child);
			}
		}

		if ( !isWidget ) // sizers
		{
			wxString afterAddChild = GetCode( obj, wxT( "after_addchild" ) );
			if ( !afterAddChild.empty() )
			{
				m_source->WriteLn( afterAddChild );
			}
			m_source->WriteLn();

			if (is_widget)
			{
				// the parent object is not a sizer. There is no template for
				// this so we'll make it manually.
				// It's not a good practice to embed templates into the source code,
				// because you will need to recompile...

				wxString _template =	wxT("#wxparent $name.Sizer = $name #nl")
					wxT("#wxparent $name.Layout()")
					wxT("#ifnull #parent $size")
					wxT("@{ #nl $name.Fit(#wxparent $name) @}");

				NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
				m_source->WriteLn(parser.ParseTemplate());
			}
		}
		else if ( type == wxT("splitter") )
		{
			// Generating the split
			switch ( obj->GetChildCount() )
			{
			case 1:
				{
					PObjectBase sub1 = obj->GetChild(0)->GetChild(0);
					wxString _template = wxT("$name.Initialize(");
					_template = _template + sub1->GetProperty( wxT("name") )->GetValue() + wxT(")");

					NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
					m_source->WriteLn(parser.ParseTemplate());
					break;
				}
			case 2:
				{
					PObjectBase sub1,sub2;
					sub1 = obj->GetChild(0)->GetChild(0);
					sub2 = obj->GetChild(1)->GetChild(0);

					wxString _template;
					if ( obj->GetProperty( wxT("splitmode") )->GetValue() == wxT("wxSPLIT_VERTICAL") )
					{
						_template = wxT("$name.SplitVertically(");
					}
					else
					{
						_template = wxT("$name.SplitHorizontally(");
					}

					_template = _template + sub1->GetProperty( wxT("name") )->GetValue() +
						wxT(", ") + sub2->GetProperty( wxT("name") )->GetValue() + wxT(", $sashpos)");

					NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
					m_source->WriteLn(parser.ParseTemplate());
					break;
				}
			default:
//				wxLogError( wxT("Missing subwindows for wxSplitterWindow widget.") );
				break;
			}
		}
		else if ( 	type == wxT("menubar")	||
			type == wxT("menu")		||
			type == wxT("submenu")	||
			type == wxT("toolbar")	||
			type == wxT("tool")	||
			type == wxT("notebook")	||
			type == wxT("auinotebook")	||
			type == wxT("treelistctrl")	||
			type == wxT("wizard") ||
			type == wxT("container")
			)
		{
			wxString afterAddChild = GetCode( obj, wxT("after_addchild") );
			if ( !afterAddChild.empty() )
			{
				m_source->WriteLn( afterAddChild );
			}
			m_source->WriteLn();
		}
	}
	else if ( info->IsSubclassOf( wxT("sizeritembase") ) )
	{
		// The child must be added to the sizer having in mind the
		// child object type (there are 3 different routines)
		GenConstruction( obj->GetChild(0), false );

		PObjectInfo childInfo = obj->GetChild(0)->GetObjectInfo();
		wxString temp_name;
		if ( childInfo->IsSubclassOf( wxT("wxWindow") ) || wxT("CustomControl") == childInfo->GetClassName() )
		{
			temp_name = wxT("window_add");
		}
		else if ( childInfo->IsSubclassOf( wxT("sizer") ) )
		{
			temp_name = wxT("sizer_add");
		}
		else if ( childInfo->GetClassName() == wxT("spacer") )
		{
			temp_name = wxT("spacer_add");
		}
		else
		{
			Debug::Print( wxT("SizerItem child is not a Spacer and is not a subclass of wxWindow or of sizer.") );
			return;
		}

		m_source->WriteLn( GetCode( obj, temp_name ) );
	}
	else if (	type == wxT("notebookpage")		||
		type == wxT("choicebookpage")	||
		type == wxT("auinotebookpage")
		)
	{
		GenConstruction( obj->GetChild( 0 ), false );
		m_source->WriteLn( GetCode( obj, wxT("page_add") ) );
		GenSettings( obj->GetObjectInfo(), obj );
	}
	else if ( type == wxT("treelistctrlcolumn") )
	{
		m_source->WriteLn( GetCode( obj, wxT("column_add") ) );
		GenSettings( obj->GetObjectInfo(), obj );
	}
	else if ( type == wxT("tool") )
	{
		// If loading bitmap from ICON resource, and size is not set, set size to toolbars bitmapsize
		// So hacky, yet so useful ...
		wxSize toolbarsize = obj->GetParent()->GetPropertyAsSize( _("bitmapsize") );
		if ( wxDefaultSize != toolbarsize )
		{
			PProperty prop = obj->GetProperty( _("bitmap") );
			if ( prop )
			{
				wxString oldVal = prop->GetValueAsString();
				wxString path, source;
				wxSize toolsize;
				TypeConv::ParseBitmapWithResource( oldVal, &path, &source, &toolsize );
				if ( _("Load From Icon Resource") == source && wxDefaultSize == toolsize )
				{
					prop->SetValue( wxString::Format( wxT("%s; %s [%i; %i]"), path.c_str(), source.c_str(), toolbarsize.GetWidth(), toolbarsize.GetHeight() ) );
					m_source->WriteLn( GetCode( obj, wxT("construction") ) );
					prop->SetValue( oldVal );
					return;
				}
			}
		}
		m_source->WriteLn( GetCode( obj, wxT("construction") ) );
	}
	else
	{
		// Generate the children
		for ( unsigned int i = 0; i < obj->GetChildCount(); i++ )
		{
			GenConstruction( obj->GetChild( i ), false );
		}
	}
}

void NitCodeGenerator::GenDestruction( PObjectBase obj )
{
	wxString _template;
	PCodeInfo code_info = obj->GetObjectInfo()->GetCodeInfo( wxT( "nit" ) );

	if ( code_info )
	{
		_template = code_info->GetTemplate( wxT( "destruction" ) );

		if ( !_template.empty() )
		{
			NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
			wxString code = parser.ParseTemplate();
			if ( !code.empty() )
			{
				m_source->WriteLn( code );
			}
		}
	}

	// Process child widgets
	for ( unsigned int i = 0; i < obj->GetChildCount() ; i++ )
	{
		PObjectBase child = obj->GetChild( i );
		GenDestruction( child );
	}
}

void NitCodeGenerator::FindMacros( PObjectBase obj, std::vector<wxString>* macros )
{
	// iterate through all of the properties of all objects, add the macros
	// to the vector
	unsigned int i;

	for ( i = 0; i < obj->GetPropertyCount(); i++ )
	{
		PProperty prop = obj->GetProperty( i );
		if ( prop->GetType() == PT_MACRO )
		{
			wxString value = prop->GetValue();
			if( value.IsEmpty() ) continue;

			value = m_PrefixDict.Convert(value);

			// Skip wx IDs
			if ( ( ! value.Contains( wxT("XRCID" ) ) ) &&
				( m_predMacros.end() == m_predMacros.find( value ) ) )
			{
				if ( macros->end() == std::find( macros->begin(), macros->end(), value ) )
				{
					macros->push_back( value );
				}
			}
		}
	}

	for ( i = 0; i < obj->GetChildCount(); i++ )
	{
		FindMacros( obj->GetChild( i ), macros );
	}
}

void NitCodeGenerator::FindEventHandlers(PObjectBase obj, EventVector &events)
{
	unsigned int i;
	for (i=0; i < obj->GetEventCount(); i++)
	{
		PEvent event = obj->GetEvent(i);
		if (!event->GetValue().IsEmpty())
			events.push_back(event);
	}

	for (i=0; i < obj->GetChildCount(); i++)
	{
		PObjectBase child = obj->GetChild(i);
		FindEventHandlers(child,events);
	}
}

void NitCodeGenerator::GenDefines( PObjectBase project)
{
	std::vector< wxString > macros;
	FindMacros( project, &macros );

	// Remove the default macro from the set, for backward compatiblity
	std::vector< wxString >::iterator it;
	it = std::find( macros.begin(), macros.end(), wxT("ID_DEFAULT") );
	if ( it != macros.end() )
	{
		// The default macro is defined to wxID_ANY
		m_source->WriteLn( wxT("ID_DEFAULT := wx.ID_ANY # Default") );
		macros.erase(it);
	}

	unsigned int id = m_firstID;
	if ( id < 1000 )
	{
		wxLogWarning(wxT("First ID is Less than 1000"));
	}
	for (it = macros.begin() ; it != macros.end(); it++)
	{
		// Don't redefine wx IDs
		m_source->WriteLn( wxString::Format( wxT("%s := %i"), it->c_str(), id ) );
		id++;
	}
	if( !macros.empty() ) m_source->WriteLn( wxT("") );
}

void NitCodeGenerator::GenSettings(PObjectInfo info, PObjectBase obj)
{
	wxString _template;
	PCodeInfo code_info = info->GetCodeInfo( wxT("nit") );

	if ( !code_info )
	{
		return;
	}

	_template = code_info->GetTemplate( wxT("settings") );

	if ( !_template.empty() )
	{
		NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
		wxString code = parser.ParseTemplate();
		if ( !code.empty() )
		{
			m_source->WriteLn(code);
		}
	}

	// Proceeding recursively with the base classes
	for (unsigned int i=0; i< info->GetBaseClassCount(false); i++)
	{
		PObjectInfo base_info = info->GetBaseClass(i, false);
		GenSettings(base_info,obj);
	}
}

void NitCodeGenerator::GenAddToolbar( PObjectInfo info, PObjectBase obj )
{
	wxArrayString arrCode;

	GetAddToolbarCode( info, obj, arrCode );

	for( size_t i = 0; i < arrCode.GetCount(); i++ ) m_source->Write( arrCode[i] );
}

void NitCodeGenerator::GetAddToolbarCode( PObjectInfo info, PObjectBase obj, wxArrayString& codelines )
{
	wxString _template;
	PCodeInfo code_info = info->GetCodeInfo( wxT( "nit" ) );

	if ( !code_info )
		return;

	_template = code_info->GetTemplate( wxT( "toolbar_add" ) );

	if ( !_template.empty() )
	{
		NitTemplateParser parser( obj, _template, m_i18n, m_useRelativePath, m_basePath, m_PrefixDict );
		wxString code = parser.ParseTemplate();
		if ( !code.empty() )
		{
			if( codelines.Index( code ) == wxNOT_FOUND ) codelines.Add( code );
		}
	}

	// Proceeding recursively with the base classes
	for ( unsigned int i = 0; i < info->GetBaseClassCount(false); i++ )
	{
		PObjectInfo base_info = info->GetBaseClass( i, false );
		GetAddToolbarCode( base_info, obj, codelines );
	}
}

///////////////////////////////////////////////////////////////////////

void NitCodeGenerator::UseRelativePath(bool relative, wxString basePath)
{
	bool result;
	m_useRelativePath = relative;

	if (m_useRelativePath)
	{
		result = wxFileName::DirExists( basePath );
		m_basePath = ( result ? basePath : wxT("") );
	}
}
/*
wxString CppCodeGenerator::ConvertToRelativePath(wxString path, wxString basePath)
{
wxString auxPath = path;
if (basePath != "")
{
wxFileName filename(_WXSTR(auxPath));
if (filename.MakeRelativeTo(_WXSTR(basePath)))
auxPath = _STDSTR(filename.GetFullPath());
}
return auxPath;
}*/

#define ADD_PREDEFINED_MACRO(x) m_predMacros.insert( wxT(#x) )
#define ADD_PREFIX(k, v) m_PrefixDict.AddPrefix(wxT(k), wxT(v))
#define ADD_CONVERTED(k, v) m_PrefixDict.AddConverted(wxT(k), wxT(v))

void NitCodeGenerator::SetupPredefinedMacros()
{
	/* no id matches this one when compared to it */
	ADD_PREDEFINED_MACRO(wx.ID.NONE);

	/*  id for a separator line in the menu (invalid for normal item) */
	ADD_PREDEFINED_MACRO(wx.ID.SEPARATOR);

	ADD_PREDEFINED_MACRO(wx.ID.ANY);

	ADD_PREDEFINED_MACRO(wx.ID.LOWEST);

	ADD_PREDEFINED_MACRO(wx.ID.OPEN);
	ADD_PREDEFINED_MACRO(wx.ID.CLOSE);
	ADD_PREDEFINED_MACRO(wx.ID.NEW);
	ADD_PREDEFINED_MACRO(wx.ID.SAVE);
	ADD_PREDEFINED_MACRO(wx.ID.SAVEAS);
	ADD_PREDEFINED_MACRO(wx.ID.REVERT);
	ADD_PREDEFINED_MACRO(wx.ID.EXIT);
	ADD_PREDEFINED_MACRO(wx.ID.UNDO);
	ADD_PREDEFINED_MACRO(wx.ID.REDO);
	ADD_PREDEFINED_MACRO(wx.ID.HELP);
	ADD_PREDEFINED_MACRO(wx.ID.PRINT);
	ADD_PREDEFINED_MACRO(wx.ID.PRINT_SETUP);
	ADD_PREDEFINED_MACRO(wx.ID.PREVIEW);
	ADD_PREDEFINED_MACRO(wx.ID.ABOUT);
	ADD_PREDEFINED_MACRO(wx.ID.HELP_CONTENTS);
	ADD_PREDEFINED_MACRO(wx.ID.HELP_COMMANDS);
	ADD_PREDEFINED_MACRO(wx.ID.HELP_PROCEDURES);
	ADD_PREDEFINED_MACRO(wx.ID.HELP_CONTEXT);
	ADD_PREDEFINED_MACRO(wx.ID.CLOSE_ALL);
	ADD_PREDEFINED_MACRO(wx.ID.PAGE_SETUP);
	ADD_PREDEFINED_MACRO(wx.ID.HELP_INDEX);
	ADD_PREDEFINED_MACRO(wx.ID.HELP_SEARCH);
	ADD_PREDEFINED_MACRO(wx.ID.PREFERENCES);

	ADD_PREDEFINED_MACRO(wx.ID.EDIT);
	ADD_PREDEFINED_MACRO(wx.ID.CUT);
	ADD_PREDEFINED_MACRO(wx.ID.COPY);
	ADD_PREDEFINED_MACRO(wx.ID.PASTE);
	ADD_PREDEFINED_MACRO(wx.ID.CLEAR);
	ADD_PREDEFINED_MACRO(wx.ID.FIND);

	ADD_PREDEFINED_MACRO(wx.ID.DUPLICATE);
	ADD_PREDEFINED_MACRO(wx.ID.SELECTALL);
	ADD_PREDEFINED_MACRO(wx.ID.DELETE);
	ADD_PREDEFINED_MACRO(wx.ID.REPLACE);
	ADD_PREDEFINED_MACRO(wx.ID.REPLACE_ALL);
	ADD_PREDEFINED_MACRO(wx.ID.PROPERTIES);

	ADD_PREDEFINED_MACRO(wx.ID.VIEW_DETAILS);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_LARGEICONS);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_SMALLICONS);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_LIST);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_SORTDATE);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_SORTNAME);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_SORTSIZE);
	ADD_PREDEFINED_MACRO(wx.ID.VIEW_SORTTYPE);

	ADD_PREDEFINED_MACRO(wx.ID.FILE);
	ADD_PREDEFINED_MACRO(wx.ID.FILE1);
	ADD_PREDEFINED_MACRO(wx.ID.FILE2);
	ADD_PREDEFINED_MACRO(wx.ID.FILE3);
	ADD_PREDEFINED_MACRO(wx.ID.FILE4);
	ADD_PREDEFINED_MACRO(wx.ID.FILE5);
	ADD_PREDEFINED_MACRO(wx.ID.FILE6);
	ADD_PREDEFINED_MACRO(wx.ID.FILE7);
	ADD_PREDEFINED_MACRO(wx.ID.FILE8);
	ADD_PREDEFINED_MACRO(wx.ID.FILE9);

	/*  Standard button and menu IDs */

	ADD_PREDEFINED_MACRO(wx.ID.OK);
	ADD_PREDEFINED_MACRO(wx.ID.CANCEL);

	ADD_PREDEFINED_MACRO(wx.ID.APPLY);
	ADD_PREDEFINED_MACRO(wx.ID.YES);
	ADD_PREDEFINED_MACRO(wx.ID.NO);
	ADD_PREDEFINED_MACRO(wx.ID.STATIC);
	ADD_PREDEFINED_MACRO(wx.ID.FORWARD);
	ADD_PREDEFINED_MACRO(wx.ID.BACKWARD);
	ADD_PREDEFINED_MACRO(wx.ID.DEFAULT);
	ADD_PREDEFINED_MACRO(wx.ID.MORE);
	ADD_PREDEFINED_MACRO(wx.ID.SETUP);
	ADD_PREDEFINED_MACRO(wx.ID.RESET);
	ADD_PREDEFINED_MACRO(wx.ID.CONTEXT_HELP);
	ADD_PREDEFINED_MACRO(wx.ID.YESTOALL);
	ADD_PREDEFINED_MACRO(wx.ID.NOTOALL);
	ADD_PREDEFINED_MACRO(wx.ID.ABORT);
	ADD_PREDEFINED_MACRO(wx.ID.RETRY);
	ADD_PREDEFINED_MACRO(wx.ID.IGNORE);
	ADD_PREDEFINED_MACRO(wx.ID.ADD);
	ADD_PREDEFINED_MACRO(wx.ID.REMOVE);

	ADD_PREDEFINED_MACRO(wx.ID.UP);
	ADD_PREDEFINED_MACRO(wx.ID.DOWN);
	ADD_PREDEFINED_MACRO(wx.ID.HOME);
	ADD_PREDEFINED_MACRO(wx.ID.REFRESH);
	ADD_PREDEFINED_MACRO(wx.ID.STOP);
	ADD_PREDEFINED_MACRO(wx.ID.INDEX);

	ADD_PREDEFINED_MACRO(wx.ID.BOLD);
	ADD_PREDEFINED_MACRO(wx.ID.ITALIC);
	ADD_PREDEFINED_MACRO(wx.ID.JUSTIFY_CENTER);
	ADD_PREDEFINED_MACRO(wx.ID.JUSTIFY_FILL);
	ADD_PREDEFINED_MACRO(wx.ID.JUSTIFY_RIGHT);

	ADD_PREDEFINED_MACRO(wx.ID.JUSTIFY_LEFT);
	ADD_PREDEFINED_MACRO(wx.ID.UNDERLINE);
	ADD_PREDEFINED_MACRO(wx.ID.INDENT);
	ADD_PREDEFINED_MACRO(wx.ID.UNINDENT);
	ADD_PREDEFINED_MACRO(wx.ID.ZOOM_100);
	ADD_PREDEFINED_MACRO(wx.ID.ZOOM_FIT);
	ADD_PREDEFINED_MACRO(wx.ID.ZOOM_IN);
	ADD_PREDEFINED_MACRO(wx.ID.ZOOM_OUT);
	ADD_PREDEFINED_MACRO(wx.ID.UNDELETE);
	ADD_PREDEFINED_MACRO(wx.ID.REVERT_TO_SAVED);

	/*  System menu IDs (used by wxUniv): */
	ADD_PREDEFINED_MACRO(wx.ID.SYSTEM_MENU);
	ADD_PREDEFINED_MACRO(wx.ID.CLOSE_FRAME);
	ADD_PREDEFINED_MACRO(wx.ID.MOVE_FRAME);
	ADD_PREDEFINED_MACRO(wx.ID.RESIZE_FRAME);
	ADD_PREDEFINED_MACRO(wx.ID.MAXIMIZE_FRAME);
	ADD_PREDEFINED_MACRO(wx.ID.ICONIZE_FRAME);
	ADD_PREDEFINED_MACRO(wx.ID.RESTORE_FRAME);

	/*  IDs used by generic file dialog (13 consecutive starting from this value) */
	ADD_PREDEFINED_MACRO(wx.ID.FILEDLGG);

	ADD_PREDEFINED_MACRO(wx.ID.HIGHEST);

	////////////////////////////////////

	// TODO: refactor to .nitcode file

	ADD_PREFIX("XRCID", "wx.XRCID.");
	ADD_PREFIX("wxID_",	"wx.ID.");

	ADD_PREFIX("wx", "wx.");

	ADD_PREFIX("wxAUI_MGR_",					"wx.AuiManager.STYLE.");
	ADD_PREFIX("wxAUI_TB_", 					"wx.AuiToolBar.STYLE.");
	ADD_PREFIX("wxAUI_NB_", 					"wx.AuiNotebook.STYLE.");

	////////////////////////////////////

	ADD_CONVERTED("wxDOUBLE_BORDER",			"wx.BORDER.DOUBLE");
	ADD_CONVERTED("wxSUNKEN_BORDER",			"wx.BORDER.SUNKEN");
	ADD_CONVERTED("wxRAISED_BORDER",			"wx.BORDER.RAISED");
	ADD_CONVERTED("wxBORDER",					"wx.BORDER.SIMPLE");
	ADD_CONVERTED("wxSIMPLE_BORDER",			"wx.BORDER.SIMPLE");
	ADD_CONVERTED("wxSTATIC_BORDER",			"wx.BORDER.STATIC");
	ADD_CONVERTED("wxNO_BORDER",				"wx.BORDER.NONE");

	ADD_CONVERTED("wxALL",						"wx.DIR.ALL");
	ADD_CONVERTED("wxLEFT",						"wx.DIR.LEFT");
	ADD_CONVERTED("wxRIGHT",					"wx.DIR.RIGHT");
	ADD_CONVERTED("wxTOP",						"wx.DIR.TOP");
	ADD_CONVERTED("wxBOTTOM",					"wx.DIR.BOTTOM");
	ADD_CONVERTED("wxUP",						"wx.DIR.UP");
	ADD_CONVERTED("wxDOWN",						"wx.DIR.DOWN");
	ADD_CONVERTED("wxNORTH",					"wx.DIR.NORTH");
	ADD_CONVERTED("wxSOUTH",					"wx.DIR.SOUTH");
	ADD_CONVERTED("wxWEST",						"wx.DIR.WEST");
	ADD_CONVERTED("wxEAST",						"wx.DIR.EAST");

	ADD_CONVERTED("wxDefaultSize",				"wx.DEFAULT.SIZE");
	ADD_CONVERTED("wxDefaultPosition",			"wx.DEFAULT.POS");
	ADD_CONVERTED("wxDefaultValidator",			"wx.DEFAULT.VALIDATOR");

	ADD_PREFIX("wxCURSOR_",						"wx.CURSOR.");
	ADD_PREFIX("wxALIGN_",						"wx.ALIGN.");
	ADD_CONVERTED("wxALIGN_CENTRE",				"wx.ALIGN.CENTER");
	ADD_PREFIX("wxICON_",						"wx.ICON.");
	ADD_PREFIX("wxBG_STYLE",					"wx.BG_STYLE.");
	ADD_PREFIX("wxITEM_",						"wx.ITEM.");
	ADD_PREFIX("wxHT_",							"wx.HT.");
	ADD_PREFIX("wxSIZE_",						"wx.SIZE.");
	ADD_PREFIX("wxDF_",							"wx.DF.");
	ADD_PREFIX("wxMOD_",						"wx.MOD.");
	ADD_PREFIX("wxUPDATE_UI_",					"wx.UPDATE_UI.");
	ADD_PREFIX("wxSTB_",						"wx.STB.");
	ADD_PREFIX("wxDrag_",						"wx.DRAG_FLAG.");

	ADD_CONVERTED("wxDragError",				"wx.DRAG_RESULT.ERROR");
	ADD_CONVERTED("wxDragNone",					"wx.DRAG_RESULT.NONE");
	ADD_CONVERTED("wxDragCopy",					"wx.DRAG_RESULT.COPY");
	ADD_CONVERTED("wxDragMove",					"wx.DRAG_RESULT.MOVE");
	ADD_CONVERTED("wxDragLink",					"wx.DRAG_RESULT.LINK");
	ADD_CONVERTED("wxDragCancel",				"wx.DRAG_RESULT.CANCEL");

	ADD_CONVERTED("wxFIXED_MINSIZE",			"wx.SIZER.FIXED_MINSIZE");
	ADD_CONVERTED("wxRESERVE_SPACE_EVEN_IF_HIDDEN", "wx.SIZER.RESERVE_SPACE_EVEN_IF_HIDDEN");

	ADD_CONVERTED("wxEXPAND",					"wx.STRETCH.EXPAND");
	ADD_CONVERTED("wxSTRETCH_NOT",				"wx.STRETCH.NOT");
	ADD_CONVERTED("wxGROW",						"wx.STRETCH.GROW");
	ADD_CONVERTED("wxSHRINK",					"wx.STRETCH.SHRINK");
	ADD_CONVERTED("wxSHAPED",					"wx.STRETCH.SHAPED");
	ADD_CONVERTED("wxTILE",						"wx.STRETCH.TILE");

	ADD_PREFIX("wxFILTER_",						"wx.Validator.FILTER.");

	ADD_PREFIX("wxART_",						"wx.ART.");
	ADD_CONVERTED("wxART_TOOLBAR",				"wx.ART_CLIENT.TOOLBAR");
	ADD_CONVERTED("wxART_MENU",					"wx.ART_CLIENT.MENU");
	ADD_CONVERTED("wxART_FRAME_ICON",			"wx.ART_CLIENT.FRAME_ICON");
	ADD_CONVERTED("wxART_CMN_DIALOG",			"wx.ART_CLIENT.CMN_DIALOG");
	ADD_CONVERTED("wxART_HELP_BROWSER",			"wx.ART_CLIENT.HELP_BROWSER");
	ADD_CONVERTED("wxART_MESSAGE_BOX",			"wx.ART_CLIENT.MESSAGE_BOX");
	ADD_CONVERTED("wxART_BUTOTN",				"wx.ART_CLIENT.BUTTON");
	ADD_CONVERTED("wxART_LIST",					"wx.ART_CLIENT.LIST");
	ADD_CONVERTED("wxART_OTHER",				"wx.ART_CLIENT.OTHER");

	ADD_PREFIX("wxSYS_COLOUR_",					"wx.SYS_COLOR.");
	ADD_CONVERTED("wxSYS_COLOUR_3DDKSHADOW",	"wx.SYS_COLOR.DKSHADOW3D");
	ADD_CONVERTED("wxSYS_COLOUR_3DLIGHT",		"wx.SYS_COLOR.LIGHT3D");
	ADD_CONVERTED("wxSYS_COLOUR_3DFACE",		"wx.SYS_COLOR.FACE3D");
	ADD_CONVERTED("wxSYS_COLOUR_3DSHADOW",		"wx.SYS_COLOR.SHADOW3D");
	ADD_CONVERTED("wxSYS_COLOUR_3DHIGHLIGHT",	"wx.SYS_COLOR.HIGHLIGHT3D");
	ADD_CONVERTED("wxSYS_COLOUR_3DHILIGHT",		"wx.SYS_COLOR.HILIGHT3D");

	////////////////////////////////////

	ADD_CONVERTED("wxICONIZE",					"wx.Window.STYLE.ICONIZE");
	ADD_CONVERTED("wxCAPTION",					"wx.Window.STYLE.CAPTION");
	ADD_CONVERTED("wxMINIMIZE",					"wx.Window.STYLE.MINIMIZE");
	ADD_CONVERTED("wxMINIMIZE_BOX",				"wx.Window.STYLE.MINIMIZE_BOX");
	ADD_CONVERTED("wxMAXIMIZE",					"wx.Window.STYLE.MAXIMIZE");
	ADD_CONVERTED("wxMAXIMIZE_BOX",				"wx.Window.STYLE.MAXIMIZE_BOX");
	ADD_CONVERTED("wxCLOSE_BOX",				"wx.Window.STYLE.CLOSE_BOX");
	ADD_CONVERTED("wxSTAY_ON_TOP",				"wx.Window.STYLE.STAY_ON_TOP");
	ADD_CONVERTED("wxRESIZE_BORDER",			"wx.Window.STYLE.RESIZE_BORDER");

	ADD_CONVERTED("wxTRANSPARENT",				"wx.Window.STYLE.TRANSPARENT");
	ADD_CONVERTED("wxTAB_TRAVERSAL",			"wx.Window.STYLE.TAB_TRAVERSAL");
	ADD_CONVERTED("wxWANTS_CHARS",				"wx.Window.STYLE.WANTS_CHARS");
	ADD_CONVERTED("wxVSCROLL",					"wx.Window.STYLE.VSCROLL");
	ADD_CONVERTED("wxHSCROLL",					"wx.Window.STYLE.HSCROLL");
	ADD_CONVERTED("wxALWAYS_SHOW_SB",			"wx.Window.STYLE.ALWAYS_SHOW_SB");
	ADD_CONVERTED("wxCLIP_CHILDREN",			"wx.Window.STYLE.CLIP_CHILDREN");
	ADD_CONVERTED("wxFULL_REPAINT_ON_RESIZE",	"wx.Window.STYLE.FULL_REPAINT_ON_RESIZE");

	ADD_CONVERTED("wxEX_VALIDATE_RECURSIVELY",	"wx.Window.STYLE.EX_VALIDATE_RECURSIVELY");
	ADD_CONVERTED("wxEX_BLOCK_EVENTS",			"wx.Window.STYLE.EX_BLOCK_EVENTS");
	ADD_CONVERTED("wxEX_TRANSIENT",				"wx.Window.STYLE.EX_TRANSIENT");
	ADD_CONVERTED("wxEX_CONTEXTHELP",			"wx.Window.STYLE.EX_CONTEXTHELP");
	ADD_CONVERTED("wxEX_PROCESS_IDLE",			"wx.Window.STYLE.EX_PROCESS_IDLE");
	ADD_CONVERTED("wxEX_PROCESS_UI_UPDATES",	"wx.Window.STYLE.EX_PROCESS_UI_UPDATES");

	////////////////////////////////////

	// common
	ADD_PREFIX("wxBU_",							"wx.Button.STYLE.");
	ADD_PREFIX("wxBORDER_",						"wx.BORDER.");
	ADD_PREFIX("wxALIGN_",						"wx.ALIGN.");
	ADD_PREFIX("wxST_",							"wx.StaticText.STYLE.");
	ADD_PREFIX("wxTE_",							"wx.TextCtrl.STYLE.");
	ADD_PREFIX("wxAC_",							"wx.AnimationCtrl.STYLE.");
	ADD_CONVERTED("wxAC_DEFAULT_STYLE",			"wx.AnimationCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxCB_",							"wx.ComboBox.STYLE.");
	ADD_PREFIX("wxLB_",							"wx.ListBox.STYLE.");
	ADD_CONVERTED("wxLB_DEFAULT",				"wx.ListBox.STYLE.DEFAULT");
	ADD_PREFIX("wxLC_",							"wx.ListCtrl.STYLE.");
	ADD_PREFIX("wxCHK_",						"wx.CheckBox.STYLE.");
	ADD_CONVERTED("wxCHK_2STATE",				"wx.CheckBox.STYLE.TWO_STATE");
	ADD_CONVERTED("wxCHK_3STATE",				"wx.CheckBox.STYLE.THREE_STATE");
	ADD_PREFIX("wxRA_",							"wx.RadioBox.STYLE.");
	ADD_PREFIX("wxRB_",							"wx.RadioButton.STYLE.");
	ADD_PREFIX("wxLI_",							"wx.StaticLine.STYLE.");
	ADD_PREFIX("wxSL_",							"wx.Slider.STYLE.");
	ADD_PREFIX("wxGA_",							"wx.Gauge.STYLE.");

	// additional
	ADD_PREFIX("wxTR_",							"wx.TreeCtrl.STYLE.");
	ADD_CONVERTED("wxTR_DEFAULT_STYLE",			"wx.TreeCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxHW_",							"wx.HtmlWindow.STYLE.");
	ADD_PREFIX("wxCLRP_",						"wx.ColorPickerCtrl.STYLE.");
	ADD_CONVERTED("wxCLRP_DEFAULT_STYLE",		"wx.ColorPickerCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxFNTP_",						"wx.FontPickerCtrl.STYLE.");
	ADD_CONVERTED("wxFNTP_DEFAULT_STYLE",		"wx.FontPickerCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxFLP_",						"wx.FilePickerCtrl.STYLE.");
	ADD_CONVERTED("wxFLP_DEFAULT_STYLE",		"wx.FilePickerCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxDIRP_",						"wx.DirPickerCtrl.STYLE.");
	ADD_CONVERTED("wxDIRP_DEFAULT_STYLE",		"wx.DirPickerCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxDP_",							"wx.DatePickerCtrl.STYLE.");
	ADD_PREFIX("wxCAL_",						"wx.CalendarCtrl.STYLE.");
	ADD_PREFIX("wxSB_",							"wx.ScrollBar.STYLE.");
	ADD_CONVERTED("wxSP_ARROW_KEYS",			"wx.SpinCtrl.STYLE.ARROW_KEYS");
	ADD_CONVERTED("wxSP_WRAP",					"wx.SpinCtrl.STYLE.WRAP");
	ADD_CONVERTED("wxSP_HORIZONTAL",			"wx.SpinButton.STYLE.HORIZONTAL");
	ADD_CONVERTED("wxSP_VERTICAL",				"wx.SpinButton.STYLE.HORIZONTAL");
	ADD_PREFIX("wxHL_",							"wx.HyperlinkCtrl.STYLE.");
	ADD_PREFIX("wxHL_DEFAULT_STYLE",			"wx.HyperlinkCtrl.STYLE.DEFAULT");
	ADD_CONVERTED("wxHL_ALIGN_CENTRE",			"wx.HyperlinkCtrl.STYLE.ALIGN_CENTER");
	ADD_PREFIX("wxDIRCTRL_",					"wx.GenericDirCtrl.STYLE.");
	ADD_CONVERTED("wxDIRCTRL_3D_INTERNAL",		"wx.GenericDirCtrl.STYLE.DEFAULT");
	ADD_PREFIX("wxSHOW_EFFECT_",				"wx.SHOW_EFFECT.");
	ADD_PREFIX("wxEL_",							"wx.EditableListBox.STYLE.");
	ADD_PREFIX("wxHLB_",						"wx.SimpleHtmlListBox.STYLE.");
	ADD_PREFIX("wxDV_",							"wx.DataViewCtrl.STYLE.");

	// containers
	ADD_PREFIX("wxSP_",							"wx.SplitterWindow.STYLE.");
	ADD_CONVERTED("wxSP_3D",					"wx.SplitterWindow.STYLE.DEFAULT");
	ADD_CONVERTED("wxSP_3DSASH",				"wx.SplitterWindow.STYLE.SASH3D");
	ADD_CONVERTED("wxSP_3DBORDER",				"wx.SplitterWindow.STYLE.BORDER3D");

	ADD_PREFIX("wxSPLIT_",						"wx.SplitterWindow.SPLIT.");
	ADD_PREFIX("wxNB_",							"wx.Notebook.STYLE.");
	ADD_PREFIX("wxAUI_NB_",						"wx.AuiNotebook.STYLE.");
	ADD_CONVERTED("wxAUI_NB_DEFAULT_STYLE",		"wx.AuiNotebook.STYLE.DEFAULT");
	ADD_PREFIX("wxCHB_",						"wx.Choicebook.STYLE.");
	ADD_PREFIX("wxCP_",							"wx.CollapsiblePane.STYLE.");
	ADD_CONVERTED("wxCP_DEFAULT_STYLE",			"wx.CollapsiblePane.STYLE.DEFAULT");

	// forms
	ADD_PREFIX("wxFRAME_",						"wx.Frame.STYLE.");
	ADD_CONVERTED("wxDEFAULT_FRAME_STYLE",		"wx.Frame.STYLE.DEFAULT");
	ADD_PREFIX("wxDIALOG_",						"wx.Dialog.STYLE.");
	ADD_CONVERTED("wxDEFAULT_DIALOG_STYLE",		"wx.Dialog.STYLE.DEFAULT");
	ADD_PREFIX("wxWIZARD_",						"wx.Wizard.STYLE.");
	ADD_PREFIX("wxMB_",							"wx.MenuBar.STYLE.");
	ADD_PREFIX("wxTB_",							"wx.ToolBar.STYLE.");

	// layout
	ADD_PREFIX("wxFLEX_GROWMODE_",				"wx.FlexGridSizer.GROWMODE.");



	////////////////////////////////////
}

void NitTemplateParser::SetupModulePrefixes()
{
}

wxString NitTemplateParser::TouchName(PObjectBase obj, const wxString& name)
{
	if (obj->GetObjectTypeName() == "form")
		return name;
	else
		return wxT("_") + name;
}