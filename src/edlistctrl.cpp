/*
 *  This file is part of Poedit (http://www.poedit.net)
 *
 *  Copyright (C) 1999-2008 Vaclav Slavik
 *  Copyright (C) 2005 Olivier Sannier
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *  $Id$
 *
 *  List view control
 *
 */

#include <wx/wx.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

#include "edlistctrl.h"
#include "digits.h"

// I don't like this global flag, but all PoeditFrame instances should share it :(
bool g_shadedList = false;

namespace
{

// how much to darken the other color in shaded list (this value
// is what GTK+ uses in its tree view control)
#define DARKEN_FACTOR      0.95

// max difference in color to consider it "amost" same if it differs by most
// this amount (in 0..255 range) from the tested color:
#define COLOR_SIMILARITY_FACTOR  20

inline bool IsAlmostBlack(const wxColour& clr)
{
    return (clr.Red() <= COLOR_SIMILARITY_FACTOR &&
            clr.Green() <= COLOR_SIMILARITY_FACTOR &&
            clr.Blue() <= COLOR_SIMILARITY_FACTOR);
}

inline bool IsAlmostWhite(const wxColour& clr)
{
    return (clr.Red() >= 255-COLOR_SIMILARITY_FACTOR &&
            clr.Green() >= 255-COLOR_SIMILARITY_FACTOR &&
            clr.Blue() >= 255-COLOR_SIMILARITY_FACTOR);
}


// colours used in the list:

const wxColour gs_ErrorColor(_T("#ff0000"));

// colors for white list control background
const wxColour gs_UntranslatedForWhite(_T("#103f67"));
const wxColour gs_FuzzyForWhite(_T("#a9861b"));

// ditto for black background
const wxColour gs_UntranslatedForBlack(_T("#1962a0"));
const wxColour gs_FuzzyForBlack(_T("#a9861b"));


const wxColour gs_TranspColor(254, 0, 253); // FIXME: get rid of this

enum
{
    IMG_NOTHING   = 0x00,
    IMG_AUTOMATIC = 0x01,
    IMG_COMMENT   = 0x02,
    IMG_MODIFIED  = 0x04,
    IMG_BK0       =  1 << 3,
    IMG_BK1       =  2 << 3,
    IMG_BK2       =  3 << 3,
    IMG_BK3       =  4 << 3,
    IMG_BK4       =  5 << 3,
    IMG_BK5       =  6 << 3,
    IMG_BK6       =  7 << 3,
    IMG_BK7       =  8 << 3,
    IMG_BK8       =  9 << 3,
    IMG_BK9       = 10 << 3
};


wxBitmap AddDigit(int digit, int x, int y, const wxBitmap& bmp)
{
    wxMemoryDC dc;
    int width = bmp.GetWidth();
    int height = bmp.GetHeight();
    wxBitmap tmpBmp(width, height);
    dc.SelectObject(tmpBmp);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(gs_TranspColor, wxSOLID));
    dc.DrawRectangle(0, 0, width, height);

    dc.DrawBitmap(bmp, 0,0,true);

    dc.SetPen(*wxBLACK_PEN);
    for(int i = 0; i < 5; i++)
    {
        for(int j = 0; j < 3; j++)
        {
            if (g_digits[digit][i][j] == 1)
                dc.DrawPoint(x+j, y+i);
        }
    }

    dc.SelectObject(wxNullBitmap);
    tmpBmp.SetMask(new wxMask(tmpBmp, gs_TranspColor));
    return tmpBmp;
}

wxBitmap MergeBitmaps(const wxBitmap& bmp1, const wxBitmap& bmp2)
{
    wxMemoryDC dc;
    wxBitmap tmpBmp(bmp1.GetWidth(), bmp1.GetHeight());

    dc.SelectObject(tmpBmp);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(gs_TranspColor, wxSOLID));
    dc.DrawRectangle(0, 0, bmp1.GetWidth(), bmp1.GetHeight());
    dc.DrawBitmap(bmp1, 0, 0, true);
    dc.DrawBitmap(bmp2, 0, 0, true);
    dc.SelectObject(wxNullBitmap);

    tmpBmp.SetMask(new wxMask(tmpBmp, gs_TranspColor));
    return tmpBmp;
}

wxBitmap BitmapFromList(wxImageList* list, int index)
{
    int width, height;
    list->GetSize(index, width, height);
    wxMemoryDC dc;
    wxBitmap bmp(width, height);
    dc.SelectObject(bmp);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(gs_TranspColor, wxSOLID));
    dc.DrawRectangle(0, 0, width, height);

    list->Draw(index, dc, 0, 0, wxIMAGELIST_DRAW_TRANSPARENT);

    dc.SelectObject(wxNullBitmap);
    bmp.SetMask(new wxMask(bmp, gs_TranspColor));
    return bmp;
}

} // anonymous namespace


BEGIN_EVENT_TABLE(PoeditListCtrl, wxListCtrl)
   EVT_SIZE(PoeditListCtrl::OnSize)
END_EVENT_TABLE()

PoeditListCtrl::PoeditListCtrl(wxWindow *parent,
               wxWindowID id,
               const wxPoint &pos,
               const wxSize &size,
               long style,
               bool dispLines,
               const wxValidator& validator,
               const wxString &name)
     : wxListView(parent, id, pos, size, style | wxLC_VIRTUAL, validator, name)
{
    m_catalog = NULL;
    m_displayLines = dispLines;
    CreateColumns();

    int i;
    wxImageList *list = new wxImageList(16, 16);

    // IMG_NOTHING:
    list->Add(wxArtProvider::GetBitmap(_T("poedit-status-nothing")));

    // IMG_AUTOMATIC:
    list->Add(wxArtProvider::GetBitmap(_T("poedit-status-automatic")));
    // IMG_COMMENT:
    list->Add(wxArtProvider::GetBitmap(_T("poedit-status-comment")));
    // IMG_AUTOMATIC | IMG_COMMENT:
    list->Add(MergeBitmaps(wxArtProvider::GetBitmap(_T("poedit-status-automatic")),
                           wxArtProvider::GetBitmap(_T("poedit-status-comment"))));

    // IMG_MODIFIED
    list->Add(wxArtProvider::GetBitmap(_T("poedit-status-modified")));

    // IMG_MODIFIED variations:
    for (i = 1; i < IMG_MODIFIED; i++)
    {
        list->Add(MergeBitmaps(BitmapFromList(list, i),
                               wxArtProvider::GetBitmap(_T("poedit-status-modified"))));
    }

    // BK_XX variations:
    for (int bk = 0; bk < 10; bk++)
    {
        for(i = 0; i <= (IMG_AUTOMATIC|IMG_COMMENT|IMG_MODIFIED); i++)
        {
            wxBitmap bmp = BitmapFromList(list, i);
            list->Add(AddDigit(bk, 0, 0, bmp));
        }
    }

    AssignImageList(list, wxIMAGE_LIST_SMALL);

    // configure items colors & fonts:

    wxVisualAttributes visual = GetDefaultAttributes();
    wxColour shaded = visual.colBg;
    shaded.Set(int(DARKEN_FACTOR * shaded.Red()),
               int(DARKEN_FACTOR * shaded.Green()),
               int(DARKEN_FACTOR * shaded.Blue()));

    m_attrNormal[1].SetBackgroundColour(shaded);
    m_attrUntranslated[1].SetBackgroundColour(shaded);
    m_attrFuzzy[1].SetBackgroundColour(shaded);
    m_attrInvalid[1].SetBackgroundColour(shaded);

    // FIXME: make this user-configurable
    if ( IsAlmostWhite(visual.colBg) )
    {
        m_attrUntranslated[0].SetTextColour(gs_UntranslatedForWhite);
        m_attrUntranslated[1].SetTextColour(gs_UntranslatedForWhite);
        m_attrFuzzy[0].SetTextColour(gs_FuzzyForWhite);
        m_attrFuzzy[1].SetTextColour(gs_FuzzyForWhite);
    }
    else if ( IsAlmostBlack(visual.colBg) )
    {
        m_attrUntranslated[0].SetTextColour(gs_UntranslatedForBlack);
        m_attrUntranslated[1].SetTextColour(gs_UntranslatedForBlack);
        m_attrFuzzy[0].SetTextColour(gs_FuzzyForBlack);
        m_attrFuzzy[1].SetTextColour(gs_FuzzyForBlack);
    }
    // else: we don't know if the default colors would be well-visible on
    //       user's background color, so play it safe and don't highlight
    //       anything

    // FIXME: todo; use appropriate font for fuzzy/trans/untrans
    m_attrInvalid[0].SetTextColour(gs_ErrorColor);
    m_attrInvalid[1].SetTextColour(gs_ErrorColor);

    SetCustomFont(wxNullFont);
}

PoeditListCtrl::~PoeditListCtrl()
{
}

void PoeditListCtrl::SetCustomFont(wxFont font_)
{
    wxFont font(font_);

    m_attrNormal[0].SetFont(font);
    m_attrNormal[1].SetFont(font);

    if ( !font.IsOk() )
        font = GetDefaultAttributes().font;

    wxFont fontb = font;
    fontb.SetWeight(wxFONTWEIGHT_BOLD);

    m_attrUntranslated[0].SetFont(fontb);
    m_attrUntranslated[1].SetFont(fontb);

    m_attrFuzzy[0].SetFont(fontb);
    m_attrFuzzy[1].SetFont(fontb);
}

void PoeditListCtrl::SetDisplayLines(bool dl)
{
    m_displayLines = dl;
    CreateColumns();
}

void PoeditListCtrl::CreateColumns()
{
    DeleteAllColumns();
    InsertColumn(0, _("Original string"));
    InsertColumn(1, _("Translation"));
    if (m_displayLines)
        InsertColumn(2, _("Line"), wxLIST_FORMAT_RIGHT);
    SizeColumns();
}

void PoeditListCtrl::SizeColumns()
{
    const int LINE_COL_SIZE = m_displayLines ? 50 : 0;

    int w = GetSize().x
            - wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) - 10
            - LINE_COL_SIZE;
    SetColumnWidth(0, w / 2);
    SetColumnWidth(1, w - w / 2);
    if (m_displayLines)
        SetColumnWidth(2, LINE_COL_SIZE);

    m_colWidth = (w/2) / GetCharWidth();
}


void PoeditListCtrl::CatalogChanged(Catalog* catalog)
{
    Freeze();

    // this is to prevent crashes (wxMac at least) when shortening virtual
    // listctrl when its scrolled to the bottom:
    m_catalog = NULL;
    SetItemCount(0);

    // now read the new catalog:
    m_catalog = catalog;
    ReadCatalog();

    Thaw();
}

void PoeditListCtrl::ReadCatalog()
{
    if (m_catalog == NULL)
    {
        SetItemCount(0);
        return;
    }

    // create the lookup arrays of Ids by first splitting it upon
    // four categories of items:
    // unstranslated, invalid, fuzzy and the rest
    m_itemIndexToCatalogIndexArray.clear();

    std::vector<int> untranslatedIds;
    std::vector<int> invalidIds;
    std::vector<int> fuzzyIds;
    std::vector<int> restIds;

    for (size_t i = 0; i < m_catalog->GetCount(); i++)
    {
        CatalogItem& d = (*m_catalog)[i];
        if (!d.IsTranslated())
          untranslatedIds.push_back(i);
        else if (d.GetValidity() == CatalogItem::Val_Invalid)
          invalidIds.push_back(i);
        else if (d.IsFuzzy())
          fuzzyIds.push_back(i);
        else
          restIds.push_back(i);
    }

    // Now fill the lookup array, not forgetting to set the appropriate
    // property in the catalog entry to be able to go back and forth
    // from one numbering system to the other
    m_catalogIndexToItemIndexArray.resize(m_catalog->GetCount());
    int listItemId = 0;
    for (size_t i = 0; i < untranslatedIds.size(); i++)
    {
        m_itemIndexToCatalogIndexArray.push_back(untranslatedIds[i]);
        m_catalogIndexToItemIndexArray[untranslatedIds[i]] = listItemId++;
    }
    for (size_t i = 0; i < invalidIds.size(); i++)
    {
        m_itemIndexToCatalogIndexArray.push_back(invalidIds[i]);
        m_catalogIndexToItemIndexArray[invalidIds[i]] = listItemId++;
    }
    for (size_t i = 0; i < fuzzyIds.size(); i++)
    {
        m_itemIndexToCatalogIndexArray.push_back(fuzzyIds[i]);
        m_catalogIndexToItemIndexArray[fuzzyIds[i]] = listItemId++;
    }
    for (size_t i = 0; i < restIds.size(); i++)
    {
        m_itemIndexToCatalogIndexArray.push_back(restIds[i]);
        m_catalogIndexToItemIndexArray[restIds[i]] = listItemId++;
    }


    // now that everything is prepared, we may set the item count
    SetItemCount(m_catalog->GetCount());

    // scroll to the top and refresh everything:
    if ( m_catalog->GetCount() )
    {
        Select(0);
        RefreshItems(0, m_catalog->GetCount()-1);
    }
    else
    {
        Refresh();
    }
}

wxString PoeditListCtrl::OnGetItemText(long item, long column) const
{
    if (m_catalog == NULL)
        return wxEmptyString;

    CatalogItem& d = (*m_catalog)[m_itemIndexToCatalogIndexArray[item]];
    switch (column)
    {
        case 0:
        {
            wxString orig = d.GetString();
            return orig.substr(0,GetMaxColChars());
        }
        case 1:
        {
            wxString trans = d.GetTranslation();
            return trans;
        }
        case 2:
            return wxString() << d.GetLineNumber();

        default:
            return wxEmptyString;
    }
}

wxListItemAttr *PoeditListCtrl::OnGetItemAttr(long item) const
{
    size_t idx = g_shadedList ? size_t(item % 2) : 0;

    if (m_catalog == NULL)
        return (wxListItemAttr*)&m_attrNormal[idx];

    CatalogItem& d = (*m_catalog)[m_itemIndexToCatalogIndexArray[item]];

    if (!d.IsTranslated())
        return (wxListItemAttr*)&m_attrUntranslated[idx];
    else if (d.IsFuzzy())
        return (wxListItemAttr*)&m_attrFuzzy[idx];
    else if (d.GetValidity() == CatalogItem::Val_Invalid)
        return (wxListItemAttr*)&m_attrInvalid[idx];
    else
        return (wxListItemAttr*)&m_attrNormal[idx];
}

int PoeditListCtrl::OnGetItemImage(long item) const
{
    if (m_catalog == NULL)
        return IMG_NOTHING;

    CatalogItem& d = (*m_catalog)[m_itemIndexToCatalogIndexArray[item]];
    int index = IMG_NOTHING;

    if (d.IsAutomatic())
        index |= IMG_AUTOMATIC;
    if (d.HasComment())
        index |= IMG_COMMENT;
    if (d.IsModified())
        index |= IMG_MODIFIED;

    index |= (static_cast<int>(d.GetBookmark())+1) << 3;

    return index;
}

void PoeditListCtrl::OnSize(wxSizeEvent& event)
{
    SizeColumns();
    event.Skip();
}

long PoeditListCtrl::GetIndexInCatalog(long item) const
{
    if (item == -1)
        return -1;
    else if (item < (long)m_itemIndexToCatalogIndexArray.size())
        return m_itemIndexToCatalogIndexArray[item];
    else
        return -1;
}

int PoeditListCtrl::GetItemIndex(int catalogIndex) const
{
    if (catalogIndex == -1)
        return -1;
    else if (catalogIndex < (int)m_catalogIndexToItemIndexArray.size())
        return m_catalogIndexToItemIndexArray[catalogIndex];
    else
        return -1;
}
