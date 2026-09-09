//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Layer Management
//---------------------------------------------------------------------------

#include "tjsCommHead.h"

#include "tjsArray.h"
#include "LayerManager.h"
#include "MsgIntf.h"
#include "LayerBitmapIntf.h"
#include "StorageIntf.h"
#include "EventIntf.h"
#include "SysInitIntf.h"
#include "TickCount.h"
#include "DebugIntf.h"
#include "LayerTreeOwner.h"



//---------------------------------------------------------------------------
// tTVPLayerManager
//---------------------------------------------------------------------------
tTVPLayerManager::tTVPLayerManager(iTVPLayerTreeOwner *owner)
{
	RefCount = 1;
	LayerTreeOwner = owner;
	DrawDeviceData = NULL;
	DrawBuffer = NULL;
	DesiredLayerType = ltOpaque;

	CaptureOwner = NULL;
	LastMouseMoveSent = NULL;
	Primary = NULL;
	FocusedLayer = NULL;
	OverallOrderIndexValid = false;
	EnabledWorkRefCount = 0;
	FocusChangeLock = false;
	VisualStateChanged = true;
	LastMouseMoveX = -1;
	LastMouseMoveY = -1;
	InNotifyingHintOrCursorChange = false;
}
//---------------------------------------------------------------------------
tTVPLayerManager::~tTVPLayerManager()
{
	if(DrawBuffer) delete DrawBuffer;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::AddRef()
{
	RefCount ++;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::Release()
{
	if(RefCount == 1)
		delete this;
	else
		RefCount --;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RegisterSelfToWindow()
{
	LayerTreeOwner->RegisterLayerManager(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::UnregisterSelfFromWindow()
{
	LayerTreeOwner->UnregisterLayerManager(this);
}

void tTVPLayerManager::SetHoldAlpha(bool b)
{
	HoldAlpha = b;
	if (!DrawBuffer) return;
	static_cast<tTVPDestTexture*>(DrawBuffer)->SetHoldAlpha(b);
}

//---------------------------------------------------------------------------
tTVPBaseTexture * tTVPLayerManager::GetDrawTargetBitmap(const tTVPRect &rect,
	tTVPRect &cliprect)
{
	// retrieve draw target bitmap
	tjs_int w = rect.get_width();
	tjs_int h = rect.get_height();

	if(!DrawBuffer) {
		// create draw buffer
        if(Primary) {
            const tTVPRect & rc = Primary->GetRect();
            w = rc.get_width();
            h = rc.get_height();
		}
        DrawBuffer = new tTVPDestTexture(w, h);
		DrawBuffer->Fill(tTVPRect(0, 0, w, h), 0xFF000000);
		static_cast<tTVPDestTexture*>(DrawBuffer)->SetHoldAlpha(HoldAlpha);
    } else {
		tjs_int bw = DrawBuffer->GetWidth();
		tjs_int bh = DrawBuffer->GetHeight();
		if(bw < w || bh  < h) {
			// insufficient size; resize the draw buffer
			tjs_uint neww = bw > w ? bw:w, newh = bh > h ? bh : h;
			neww += (neww & 1); // align to even
			DrawBuffer->SetSize(neww, newh, false);
			DrawBuffer->Fill(tTVPRect(0, 0, neww, newh), 0xFF000000);
		}
	}

	cliprect = rect;
	return DrawBuffer;
}
//---------------------------------------------------------------------------
tTVPLayerType tTVPLayerManager::GetTargetLayerType()
{
	return DesiredLayerType;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DrawCompleted(const tTVPRect &destrect,
	tTVPBaseTexture *bmp, const tTVPRect &cliprect,
		tTVPLayerType type, tjs_int opacity)
{
#if 0
	if (!LayerTreeOwner) return;
	LayerTreeOwner->NotifyBitmapCompleted(this, destrect.left, destrect.top, bmp, cliprect, type, opacity);
#else
    tjs_int w, h;
	if(!/*LayerTreeOwner->*/GetPrimaryLayerSize(w, h)) return;
    //Window->GetDrawDevice()->GetSrcSize(w, h);
    if (!DrawBuffer) {
        // create draw buffer
		DrawBuffer = new tTVPDestTexture(w, h);
		DrawBuffer->Fill(tTVPRect(0, 0, w, h), 0xFF000000);
		static_cast<tTVPDestTexture*>(DrawBuffer)->SetHoldAlpha(HoldAlpha);
	} else {
        tjs_int bw = DrawBuffer->GetWidth();
        tjs_int bh = DrawBuffer->GetHeight();
        if (bw < w || bh  < h) {
            // insufficient size; resize the draw buffer
            tjs_uint neww = bw > w ? bw : w, newh = bh > h ? bh : h;
            neww += (neww & 1); // align to even
            DrawBuffer->SetSize(neww, newh, false);
			DrawBuffer->Fill(tTVPRect(0, 0, neww, newh), 0xFF000000);
		}
    }

	DrawBuffer->Blt(destrect.left, destrect.top, bmp, cliprect, type, opacity, HoldAlpha);
#endif
}

tTVPBaseTexture* tTVPLayerManager::GetOrCreateDrawBuffer()
{
	if (!DrawBuffer) {
		tjs_int w, h;
		if (!GetPrimaryLayerSize(w, h)) return nullptr;
		DrawBuffer = new tTVPDestTexture(w, h);
		DrawBuffer->Fill(tTVPRect(0, 0, w, h), 0xFF000000);
		static_cast<tTVPDestTexture*>(DrawBuffer)->SetHoldAlpha(HoldAlpha);
	}
	return DrawBuffer;
}

//---------------------------------------------------------------------------
void tTVPLayerManager::AttachPrimary(tTJSNI_BaseLayer *pri)
{
	// attach primary layer to the manager
	DetachPrimary();

	if(!Primary)
	{
		Primary = pri;
		EnabledWorkRefCount = 0;
		OverallOrderIndexValid = false;
		UpdateRegion.Clear();
		pri->SetVisible(true);
		pri->SetOpacity(255);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DetachPrimary()
{
	// detach primary layer from the manager
	if(Primary)
	{
		SetFocusTo(NULL);
		ReleaseCapture();
		ReleaseTouchCaptureAll();
		ForceMouseLeave();
		NotifyPart(Primary);
		Primary = NULL;
	}
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPLayerManager::GetPrimaryLayerSize(tjs_int &w, tjs_int &h) const
{
	if(IsPrimaryLayerAttached())
	{
		w = Primary->GetWidth();
		h = Primary->GetHeight();
		return true;
	}
	else
	{
		return false;
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyPart(tTJSNI_BaseLayer *lay)
{
	// notifies layer parting from its parent
	InvalidateOverallIndex();
	BlurTree(lay);
	ReleaseCaptureFromTree(lay);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::InvalidateOverallIndex()
{
	OverallOrderIndexValid = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RecreateOverallOrderIndex()
{
	// recreate overall order index
	if(!OverallOrderIndexValid)
	{
		tjs_uint index = 0;
		AllNodes.clear();
		if(Primary) Primary->RecreateOverallOrderIndex(index, AllNodes);

		OverallOrderIndexValid = true;
	}
}
//---------------------------------------------------------------------------
std::vector<tTJSNI_BaseLayer*> & tTVPLayerManager::GetAllNodes()
{
	if(!OverallOrderIndexValid) RecreateOverallOrderIndex();
	return AllNodes;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::QueryUpdateExcludeRect()
{
	if(!VisualStateChanged) return;
	tTVPRect r;
	r.clear();
	if(Primary) Primary->QueryUpdateExcludeRect(r, true);
	VisualStateChanged = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyMouseCursorChange(
	tTJSNI_BaseLayer *layer, tjs_int cursor)
{
	if(InNotifyingHintOrCursorChange) return;

	InNotifyingHintOrCursorChange = true;
	try
	{

		tTJSNI_BaseLayer *l;

		if(CaptureOwner)
			l = CaptureOwner;
		else
			l = GetMostFrontChildAt(LastMouseMoveX, LastMouseMoveY);

		if(l == layer) SetMouseCursor(cursor);
	}
	catch(...)
	{
		InNotifyingHintOrCursorChange = false;
		throw;
	}

	InNotifyingHintOrCursorChange = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetMouseCursor(tjs_int cursor)
{
	if(!LayerTreeOwner) return;

	LayerTreeOwner->SetMouseCursor(this, cursor);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::GetCursorPos(tjs_int &x, tjs_int &y)
{
	if(!LayerTreeOwner) return;
	LayerTreeOwner->GetCursorPos(this, x, y);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetCursorPos(tjs_int x, tjs_int y)
{
	if(!LayerTreeOwner) return;
	LayerTreeOwner->SetCursorPos(this, x, y);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyHintChange(tTJSNI_BaseLayer *layer, const ttstr & hint)
{
	if(InNotifyingHintOrCursorChange) return;

	InNotifyingHintOrCursorChange = true;

	try
	{
		tTJSNI_BaseLayer *l;

		if(CaptureOwner)
			l = CaptureOwner;
		else
			l = GetMostFrontChildAt(LastMouseMoveX, LastMouseMoveY);

		if(l == layer) SetHint(l->GetOwnerNoAddRef(),hint);
	}
	catch(...)
	{
		InNotifyingHintOrCursorChange = false;
		throw;
	}

	InNotifyingHintOrCursorChange = false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetHint(iTJSDispatch2* sender, const ttstr &hint)
{
	if(!LayerTreeOwner) return;
	LayerTreeOwner->SetHint(this, sender, hint);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyLayerResize()
{
	// notifies layer resizing to the LayerTreeOwner
	if(!LayerTreeOwner) return;

	LayerTreeOwner->NotifyLayerResize(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyWindowInvalidation()
{
	// notifies layer surface is invalidated and should be transfered to LayerTreeOwner.
	if(!LayerTreeOwner) return;

	LayerTreeOwner->NotifyLayerImageChange(this);
	// TODO atlernative of LayerTreeOwner->RequestUpdate();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetLayerTreeOwner(class iTVPLayerTreeOwner* owner)
{
	// sets LayerTreeOwner
	LayerTreeOwner = owner;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyResizeFromWindow(tjs_uint w, tjs_uint h)
{
	// is called by the owner window, notifies windows's client area size
	// has been changed.
	// does not be called if owner window's "autoResize" property is false.

	// currently this function is not used.

	if(Primary) Primary->InternalSetSize(w, h);
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer * tTVPLayerManager::GetMostFrontChildAt(tjs_int x, tjs_int y,
	tTJSNI_BaseLayer *except, bool get_disabled)
{
	// return most front layer at given point.
	// this does checking of layer's visibility.
	// x and y are given in primary layer's coordinates.
	if(!Primary) return NULL;

	tTJSNI_BaseLayer *lay = NULL;
	Primary->GetMostFrontChildAt(x, y, &lay, except, get_disabled);
	return lay;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryClick(tjs_int x, tjs_int y)
{
	tTJSNI_BaseLayer * l = GetMostFrontChildAt(x, y);
	if(l && CaptureOwner == l)
	{
		l->FromPrimaryCoordinates(x, y);
		l->FireClick(x, y);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryDoubleClick(tjs_int x, tjs_int y)
{
	tTJSNI_BaseLayer * l = GetMostFrontChildAt(x, y);
	if(l /*&& CaptureOwner == l*/)
	{
		l->FromPrimaryCoordinates(x, y);
		l->FireDoubleClick(x, y);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb,
	tjs_uint32 flags)
{
	PrimaryMouseMove(x, y, flags);
	tTJSNI_BaseLayer * l = CaptureOwner ? CaptureOwner : GetMostFrontChildAt(x, y);
	if(l)
	{
		l->FromPrimaryCoordinates(x, y);
		ReleaseCaptureCalled = false;
		l->FireMouseDown(x, y, mb, flags);
		bool no_capture = ReleaseCaptureCalled;

		if(CaptureOwner != l)
		{
			ReleaseCapture();

			if(!no_capture)
			{
				CaptureOwner = l;
				if(CaptureOwner->Owner) CaptureOwner->Owner->AddRef(); // addref TJS object
			}
		}
		
		SetHint(NULL,ttstr());
	}
	else
	{
		ReleaseCapture();
	}

}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb,
	tjs_uint32 flags)
{
	tTJSNI_BaseLayer *l;

	if(CaptureOwner)
		l = CaptureOwner;
	else
		l = GetMostFrontChildAt(x, y);

	if(l)
	{
		int orig_x = x, orig_y = y;

		l->FromPrimaryCoordinates(x, y);
		l->FireMouseUp(x, y, mb, flags);

		if(!TVPIsAnyMouseButtonPressedInShiftStateFlags(flags))
		{
			ReleaseCapture();
			PrimaryMouseMove(orig_x, orig_y, flags); // force recheck current under-cursor layer
		}
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseMove(tjs_int x, tjs_int y, tjs_uint32 flags)
{
	bool poschanged = (LastMouseMoveX != x || LastMouseMoveY != y);
	LastMouseMoveX = x;
	LastMouseMoveY = y;

	tTJSNI_BaseLayer *l;

	if(CaptureOwner)
		l = CaptureOwner;
	else
		l = GetMostFrontChildAt(x, y);

	// enter/leave event
	if(LastMouseMoveSent != l)
	{
		if(LastMouseMoveSent) LastMouseMoveSent->FireMouseLeave();

		// recheck l because the layer may become invalid during
		// FireMouseLeave call.
		if(CaptureOwner)
			l = CaptureOwner;
		else
			l = GetMostFrontChildAt(x, y);

		if(l)
		{
			InNotifyingHintOrCursorChange = true;
			try
			{
				tTJSNI_BaseLayer *ll;

				l->FireMouseEnter();

				// recheck l because the layer may become invalid during
				// FireMouseEnter call.
				if(CaptureOwner)
					ll = CaptureOwner;
				else
					ll = GetMostFrontChildAt(x, y);

				if(l != ll)
				{
					l->FireMouseLeave();
					l = ll;
					if(l) l->FireMouseEnter();
				}

				// note: rechecking is done only once to avoid infinite loop

				if(l) l->SetCurrentCursorToWindow();
				if(l) l->SetCurrentHintToWindow();
			}
			catch(...)
			{
				InNotifyingHintOrCursorChange = false;
				throw;
			}
			InNotifyingHintOrCursorChange = false;
		}

		if(!l)
		{
			SetMouseCursor(0);
			SetHint(NULL,ttstr());
		}
	}

	if(LastMouseMoveSent != l)
	{
		if(LastMouseMoveSent)
		{
			tTJSNI_BaseLayer *lay = LastMouseMoveSent;
			LastMouseMoveSent = NULL;
			if(lay->Owner) lay->Owner->Release();
		}

		LastMouseMoveSent = l;


		if(LastMouseMoveSent)
		{
			if(LastMouseMoveSent->Owner) LastMouseMoveSent->Owner->AddRef();
		}
	}

	if(l)
	{
		if(poschanged)
		{
			l->FromPrimaryCoordinates(x, y);
			l->FireMouseMove(x, y, flags);
		}
	}
	else
	{
		// no layer to send the event
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchDown( tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id )
{
	tjs_int ix = (tjs_int)x, iy = (tjs_int)y;
	ReleaseTouchCapture(id);
	tTJSNI_BaseLayer * l = GetMostFrontChildAt(ix, iy);
	if( l )
	{
		l->FromPrimaryCoordinates(x, y);
		ReleaseTouchCaptureIDMark = (tjs_int64)id;
		l->FireTouchDown(x, y, cx, cy, id);
		if( ReleaseTouchCaptureIDMark == (tjs_int64)id ) {
			SetTouchCapture( id, l );
		}
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchUp( tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id )
{
	tjs_int ix = (tjs_int)x, iy = (tjs_int)y;
	tTJSNI_BaseLayer * l = GetTouchCapture(id) ? GetTouchCapture(id) : GetMostFrontChildAt(ix, iy);
	if( l )
	{
		l->FromPrimaryCoordinates(x, y);
		l->FireTouchUp(x, y, cx, cy, id);
		ReleaseTouchCapture(id);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchMove( tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id )
{
	tjs_int ix = (tjs_int)x, iy = (tjs_int)y;
	tTJSNI_BaseLayer * l = GetTouchCapture(id) ? GetTouchCapture(id) : GetMostFrontChildAt(ix, iy);
	if( l )
	{
		l->FromPrimaryCoordinates(x, y);
		l->FireTouchMove(x, y, cx, cy, id);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchScaling( tjs_real startdist, tjs_real curdist, tjs_real cx, tjs_real cy, tjs_int flag )
{
	if(FocusedLayer)
		FocusedLayer->FireTouchScaling(startdist,curdist,cx,cy,flag);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryTouchRotate( tjs_real startangle, tjs_real curangle, tjs_real dist, tjs_real cx, tjs_real cy, tjs_int flag )
{
	if(FocusedLayer)
		FocusedLayer->FireTouchRotate(startangle,curangle,dist,cx,cy,flag);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMultiTouch()
{
	if(FocusedLayer)
		FocusedLayer->FireMultiTouch();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ForceMouseLeave()
{
	if(LastMouseMoveSent)
	{
		tTJSNI_BaseLayer *lay = LastMouseMoveSent;
		LastMouseMoveSent = NULL;
		lay->FireMouseLeave();
		if(lay->Owner) lay->Owner->Release();
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ForceMouseRecheck()
{
	PrimaryMouseMove(LastMouseMoveX, LastMouseMoveY, 0);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::MouseOutOfWindow()
{
	// notifys that the mouse cursor goes outside of the window.
	if(!CaptureOwner)
		PrimaryMouseMove(-1, -1, 0); // force mouse cursor out of the all
}
//---------------------------------------------------------------------------
void tTVPLayerManager::LeaveMouseFromTree(tTJSNI_BaseLayer *root)
{
	// force to leave mouse
	if(LastMouseMoveSent)
	{
		if(LastMouseMoveSent->IsAncestorOrSelf(root))
		{
			tTJSNI_BaseLayer *lay = LastMouseMoveSent;
			LastMouseMoveSent = NULL;
			lay->FireMouseLeave();
			if(lay->Owner) lay->Owner->Release();
		}
	}
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::ReleaseCapture()
{
	// release capture state
	ReleaseCaptureCalled = true;
	if(CaptureOwner)
	{
		tTJSNI_BaseLayer *lay = CaptureOwner;
		CaptureOwner = NULL;
		if(lay->Owner) lay->Owner->Release();
			// release TJS object

		LayerTreeOwner->ReleaseMouseCapture(this);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseCaptureFromTree(tTJSNI_BaseLayer * layer)
{
	// Release capture state, if the capture object is descendant of
	// 'layer' or 'layer' itself.
	if(CaptureOwner)
	{
		if(CaptureOwner->IsAncestorOrSelf(layer))
		{
			ReleaseCapture();
		}
	}
	std::vector<tTVPTouchCaptureLayer>::iterator itr = TouchCapture.begin();
	while( itr != TouchCapture.end() )
	{
		tTJSNI_BaseLayer* l = itr->Owner;
		if( l && l->IsAncestorOrSelf(layer))
		{
			if( l->Owner ) l->Owner->Release();
			if( ReleaseTouchCaptureIDMark == (tjs_int64)(itr->TouchID) ) ReleaseTouchCaptureIDMark = -1;
			itr = TouchCapture.erase(itr);
		}
		else
		{
			itr++;
		}
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseTouchCapture( tjs_uint32 id )
{
	FindTouchID pred( id );
	std::vector<tTVPTouchCaptureLayer>::iterator itr = std::find_if( TouchCapture.begin(), TouchCapture.end(), pred );
	if( itr != TouchCapture.end() )
	{
		tTJSNI_BaseLayer* old = itr->Owner;
		if( old && old->Owner ) old->Owner->Release();
		TouchCapture.erase(itr);
	}
	if( ReleaseTouchCaptureIDMark == (tjs_int64)id ) ReleaseTouchCaptureIDMark = -1;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseTouchCaptureAll()
{
	for( std::vector<tTVPTouchCaptureLayer>::iterator itr = TouchCapture.begin(); itr != TouchCapture.end(); itr++ )
	{
		tTJSNI_BaseLayer* l = itr->Owner;
		if( l->Owner ) l->Owner->Release();
	}
	TouchCapture.clear();
	ReleaseTouchCaptureIDMark = -1;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetTouchCapture( tjs_uint32 id, tTJSNI_BaseLayer* layer )
{
	FindTouchID pred( id );
	std::vector<tTVPTouchCaptureLayer>::iterator itr = std::find_if( TouchCapture.begin(), TouchCapture.end(), pred );
	if( itr != TouchCapture.end() )
	{
		// 既に同一IDのものがある場合は、同じ場所で置き換える
		tTJSNI_BaseLayer* old = itr->Owner;
		if( old && old->Owner ) old->Owner->Release();
		itr->Owner = layer;
		if( layer->Owner ) layer->Owner->AddRef();
	}
	else
	{
		// ない場合は、末尾に追加。
		TouchCapture.push_back( tTVPTouchCaptureLayer( id, layer ) );
		if( layer->Owner ) layer->Owner->AddRef();
	}
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::BlurTree(tTJSNI_BaseLayer *root)
{
	// (primary only) remove focus from "root"
	RemoveTreeModalState(root);
	LeaveMouseFromTree(root);

	if(!FocusedLayer) return false;

	if(!FocusedLayer->IsAncestorOrSelf(root)) return false;
		// root is not ancestor of current focused layer

	tTJSNI_BaseLayer *next = root->GetNextFocusable();

	if(next != FocusedLayer)
		SetFocusTo(next, true); // focus to root's next focusable layer
	else
		SetFocusTo(NULL, true);

	return true;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::CheckTreeFocusableState(tTJSNI_BaseLayer *root)
{
	// (primary only) check newly added tree's focusable state
/*	// uncomment here to auto-focus
	if(FocusedLayer) return;

	tTJSNI_BaseLayer *lay = root->SearchFirstFocusable(true);
	if(lay) SetFocusTo(lay, true);
*/
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::FocusPrev()
{
	// focus to previous layer
	tTJSNI_BaseLayer *l;
	if(!FocusedLayer)
		l = SearchFirstFocusable(false);// search first focusable layer
	else
		l = FocusedLayer->GetPrevFocusable();

	if(l) SetFocusTo(l, false);
	return l;
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::FocusNext()
{
	// focus to next layer
	tTJSNI_BaseLayer *l;
	if(!FocusedLayer)
		l = SearchFirstFocusable(false);// search first focusable layer
	else
		l = FocusedLayer->GetNextFocusable();

	if(l) SetFocusTo(l, true);
	return l;
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::SearchFirstFocusable(bool ignore_chain_focusable)
{
	// (primary only) search first focusable layer
	if(!Primary) return NULL;
	tTJSNI_BaseLayer *lay = Primary->SearchFirstFocusable(ignore_chain_focusable);

	return lay;
}
//---------------------------------------------------------------------------
// Where the focusable layers are, for a gamepad.
//
// KiriKiri was written for a mouse and a keyboard, and its two ways of moving
// focus reflect that: a click focuses whatever is under it, and Tab walks
// GetNextFocusable, which is the layer tree's own order -- the order the
// layers were constructed in. Neither answers what a D-pad asks, which is
// always geometric: what is the nearest focusable thing to the left of here.
//
// So the focusable layers are gathered once per press by walking the tree,
// each with its rectangle in primary coordinates. The walk reads Visible,
// GetNodeFocusable and Rect and nothing else: no hit test, and therefore no
// onHitTest event, so this cannot run script and cannot be reentered by it.
//---------------------------------------------------------------------------
static void TVPTraceLayer(tTVPFocusStepReport *report, tTJSNI_BaseLayer *layer,
	tjs_int left, tjs_int top, const char *verdict)
{
	if(!report || !report->Trace) return;
	// A layer tree is deep and a run of this is only ever read once, so the
	// trace stops at a size a log line can carry rather than growing without
	// bound on a screen with hundreds of layers.
	if(report->Trace->size() > 12000) return;
	char line[256];
	std::string name = layer->GetName().AsNarrowStdString();
	if(name.size() > 40) name.resize(40);
	snprintf(line, sizeof(line), "  %s [%d,%d %dx%d] vis=%d foc=%d chain=%d: %s\n",
		name.empty() ? "(unnamed)" : name.c_str(),
		(int)left, (int)top,
		(int)layer->GetRect().get_width(), (int)layer->GetRect().get_height(),
		layer->GetVisible() ? 1 : 0, layer->GetFocusable() ? 1 : 0,
		layer->GetJoinFocusChain() ? 1 : 0, verdict);
	report->Trace->append(line);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::CollectFocusable(tTJSNI_BaseLayer *layer,
	tjs_int offsetX, tjs_int offsetY, std::vector<tTVPFocusableLayer> &into,
	tTVPFocusStepReport *report)
{
	if(!layer) return;
	if(!layer->Visible)
	{
		// An invisible subtree is not on screen at all, so it is pruned whole
		// -- which is worth saying, because a whole menu missing from the
		// candidates looks the same as a menu that was never built.
		TVPTraceLayer(report, layer, offsetX, offsetY, "invisible: subtree skipped");
		return;
	}

	tjs_int left = offsetX, top = offsetY;
	if(!layer->IsPrimary()) { left += layer->Rect.left; top += layer->Rect.top; }

	if(layer != Primary)
	{
		if(!layer->GetNodeFocusable())
			TVPTraceLayer(report, layer, left, top, "not focusable");
		else if(!layer->JoinFocusChain)
			TVPTraceLayer(report, layer, left, top, "focusable but out of the focus chain");
		else
		{
			TVPTraceLayer(report, layer, left, top, "candidate");
			tTVPFocusableLayer found;
			found.Layer = layer;
			found.Rect.left = left;
			found.Rect.top = top;
			found.Rect.right = left + layer->Rect.get_width();
			found.Rect.bottom = top + layer->Rect.get_height();
			into.push_back(found);
		}
	}

	tjs_int count = layer->Children.GetCount();
	for(tjs_int i = 0; i < count; i++)
	{
		tTJSNI_BaseLayer *child = layer->Children[i];
		if(child) CollectFocusable(child, left, top, into, report);
	}
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::GetFocusableLayerAt(tjs_int x, tjs_int y)
{
	if(!Primary) return NULL;
	std::vector<tTVPFocusableLayer> found;
	CollectFocusable(Primary, 0, 0, found, NULL);

	// The last one collected is the frontmost: children are walked in their
	// own order, and a later child of the same parent is drawn over an
	// earlier one, which is the order GetMostFrontChildAt searches backward.
	for(std::vector<tTVPFocusableLayer>::reverse_iterator i = found.rbegin();
		i != found.rend(); ++i)
	{
		if(x >= i->Rect.left && x < i->Rect.right &&
			y >= i->Rect.top && y < i->Rect.bottom) return i->Layer;
	}
	return NULL;
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::GetFocusableLayerInDirection(
	tjs_int x, tjs_int y, tjs_int dirX, tjs_int dirY, tTVPFocusStepReport *report)
{
	if(!Primary) return NULL;
	if(dirX == 0 && dirY == 0) return NULL;
	std::vector<tTVPFocusableLayer> found;
	CollectFocusable(Primary, 0, 0, found, report);

	tTJSNI_BaseLayer *best = NULL;
	tjs_int bestScore = 0;
	for(std::vector<tTVPFocusableLayer>::iterator i = found.begin();
		i != found.end(); ++i)
	{
		// A layer the pointer is already standing inside is not somewhere to
		// step to: it is where the step is coming from. Noble Works' title
		// screen is exactly this case -- its five buttons are links inside one
		// full-screen message layer, and that layer is the only focusable
		// thing on the screen -- and without this test every direction
		// "found" it and warped the ring to the middle of the screen, where
		// there is no button and confirm did nothing (dq-kirikiri-09).
		bool inside = x >= i->Rect.left && x < i->Rect.right &&
			y >= i->Rect.top && y < i->Rect.bottom;
		if(inside && report) report->PointerInsideFocusable = true;

		tjs_int cx = (i->Rect.left + i->Rect.right) / 2;
		tjs_int cy = (i->Rect.top + i->Rect.bottom) / 2;
		// How far it lies ALONG the direction, and how far it strays ACROSS
		// it. Only what is in front counts, so a direction can never step
		// back to where it came from.
		tjs_int along = dirX * (cx - x) + dirY * (cy - y);
		tjs_int across = dirX != 0 ? (cy - y) : (cx - x);
		if(across < 0) across = -across;
		if(report && report->Trace && report->Trace->size() <= 12000)
		{
			char line[192];
			snprintf(line, sizeof(line),
				"  candidate at %d,%d %dx%d: along=%d across=%d%s\n",
				(int)i->Rect.left, (int)i->Rect.top,
				(int)(i->Rect.right - i->Rect.left),
				(int)(i->Rect.bottom - i->Rect.top),
				(int)along, (int)across,
				inside ? " REJECTED: the pointer is inside it" :
					(along <= 4 ? " REJECTED: not ahead of the direction" : ""));
			report->Trace->append(line);
		}
		if(inside) continue;
		if(along <= 4) continue;
		// Straight ahead beats close but far off to one side: a menu column
		// steps down its own items rather than across to a button beside it.
		tjs_int score = along + across * 3;
		if(!best || score < bestScore) { best = i->Layer; bestScore = score; }
	}
	return best;
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::SetFocusTo(tTJSNI_BaseLayer *layer, bool direction)
{
	// set focus to layer

	// direction = true : forward focus
	// direction = false: backward focus

	if(layer && !layer->GetNodeFocusable()) return false;

	if(layer && !layer->Shutdown)
		layer = layer->FireBeforeFocus(FocusedLayer, direction);

	if(layer && !layer->GetNodeFocusable()) return false;

	if(FocusedLayer == layer) return false;


	if(FocusChangeLock)
		TVPThrowExceptionMessage(TVPCannotChangeFocusInProcessingFocus);
	FocusChangeLock = true;

	tTJSNI_BaseLayer *org = FocusedLayer;
	FocusedLayer = layer;

	try
	{
		if(org && !org->Shutdown)
			org->FireBlur(layer);

		if(FocusedLayer && !FocusedLayer->Shutdown)
			FocusedLayer->FireFocus(org, direction);
	}
	catch(...)
	{
		if(FocusedLayer) if(FocusedLayer->Owner)
			FocusedLayer->Owner->AddRef();
		if(org) if(org->Owner)
			org->Owner->Release();
		FocusChangeLock = false;
		throw;
	}

	if(FocusedLayer) if(FocusedLayer->Owner)
		FocusedLayer->Owner->AddRef();
	if(org) if(org->Owner)
		org->Owner->Release();

	if(FocusedLayer) SetImeModeOf(FocusedLayer); else ResetImeMode();
	if(FocusedLayer) SetAttentionPointOf(FocusedLayer); else DisableAttentionPoint();

	FocusChangeLock = false;
	return true;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ReleaseAllModalLayer()
{
	// (primary only) release all modal layer on invalidation
	std::vector<tTJSNI_BaseLayer*> copy(ModalLayerVector);
	ModalLayerVector.clear();

	std::vector<tTJSNI_BaseLayer*>::iterator i;
	for(i = copy.begin(); i < copy.end(); i++)
	{
		if((*i)->Owner) (*i)->Owner->Release();
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetModeTo(tTJSNI_BaseLayer *layer)
{
	// (primary only) set mode to layer
	if(!layer) return;

	SaveEnabledWork();

	try
	{
		tTJSNI_BaseLayer *current = GetCurrentModalLayer();
		if(current && layer->IsAncestorOrSelf(current))
			TVPThrowExceptionMessage(TVPCannotSetModeToDisabledOrModal);
				// cannot set mode to parent layer
		if(!layer->Visible) layer->Visible = true;
		if(!layer->GetParentVisible() || !layer->Enabled)
			TVPThrowExceptionMessage(TVPCannotSetModeToDisabledOrModal);
				// cannot set mode to parent layer
		if(layer == current)
			TVPThrowExceptionMessage(TVPCannotSetModeToDisabledOrModal);
				// cannot set mode to already modal layer

		SetFocusTo(layer->SearchFirstFocusable(), true);

		if(layer->Owner) layer->Owner->AddRef();
		ModalLayerVector.push_back(layer);

	}
	catch(...)
	{
		NotifyNodeEnabledState();
		throw;
	}

	NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RemoveModeFrom(tTJSNI_BaseLayer *layer)
{
	// remove modal state from given layer
	bool do_notify = false;

	try
	{
		std::vector<tTJSNI_BaseLayer*>::iterator i;
		for(i = ModalLayerVector.begin(); i < ModalLayerVector.end();)
		{
			if(layer == *i)
			{
				if(!do_notify) { do_notify = true; SaveEnabledWork(); }
				if(layer->Owner) layer->Owner->Release();
				SetFocusTo(layer->GetNextFocusable(), true);
				i = ModalLayerVector.erase(i);
			}
			else
			{
				i++;
			}
		}
	}
	catch(...)
	{
		if(do_notify) NotifyNodeEnabledState();
		throw;
	}

	if(do_notify) NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::RemoveTreeModalState(tTJSNI_BaseLayer *root)
{
	// remove modal state from given tree
	bool do_notify = false;

	try
	{
		std::vector<tTJSNI_BaseLayer*>::iterator i;
		for(i = ModalLayerVector.begin(); i < ModalLayerVector.end();)
		{
			if((*i)->IsAncestorOrSelf(root))
			{
				if(!do_notify) { do_notify = true; SaveEnabledWork(); }
				if((*i)->Owner) (*i)->Owner->Release();
				SetFocusTo(root->GetNextFocusable(), true);
				i = ModalLayerVector.erase(i);
			}
			else
			{
				i++;
			}
		}
	}
	catch(...)
	{
		if(do_notify) NotifyNodeEnabledState();
		throw;
	}

	if(do_notify) NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
tTJSNI_BaseLayer *tTVPLayerManager::GetCurrentModalLayer() const
{
	// (primary only) get current modal layer
	tjs_uint size = (tjs_uint)ModalLayerVector.size();
	if(size == 0) return NULL;
	return *(ModalLayerVector.begin() + size - 1);
}
//---------------------------------------------------------------------------
bool tTVPLayerManager::SearchAttentionPoint(tTJSNI_BaseLayer *target,
	tjs_int &x, tjs_int &y)
{
	// search specified layer 's attention point
	while(target)
	{
		if(target->UseAttention)
		{
			x = target->AttentionLeft, y = target->AttentionTop;
			target->ToPrimaryCoordinates(x, y);
			return true;
		}
		target = target->Parent;
	}
	return false;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetAttentionPointOf(tTJSNI_BaseLayer *layer)
{
	if(!LayerTreeOwner) return;
	tjs_int x, y;
	if(SearchAttentionPoint(layer, x, y))
		LayerTreeOwner->SetAttentionPoint(this, layer, x, y);
	else
		LayerTreeOwner->DisableAttentionPoint(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::DisableAttentionPoint()
{
	if(LayerTreeOwner) LayerTreeOwner->DisableAttentionPoint(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyAttentionStateChanged(tTJSNI_BaseLayer *from)
{
	if(FocusedLayer == from)
	{
		SetAttentionPointOf(from);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SetImeModeOf(tTJSNI_BaseLayer *layer)
{
	if(!LayerTreeOwner) return;
	LayerTreeOwner->SetImeMode(this, layer->ImeMode);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::ResetImeMode()
{
	if(!LayerTreeOwner) return;
	LayerTreeOwner->ResetImeMode(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyImeModeChanged(tTJSNI_BaseLayer *from)
{
	if(FocusedLayer == from)
	{
		SetImeModeOf(from);
	}
}
//---------------------------------------------------------------------------
void tTVPLayerManager::SaveEnabledWork()
{
	// save current node enabled state to EnabledWork
	// this does recursive call
	if(EnabledWorkRefCount == 0) if(Primary) Primary->SaveEnabledWork();

	EnabledWorkRefCount ++;
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyNodeEnabledState()
{
	// notify node enabled state change to self and its children
	// this refers EnabledWork which is created by SaveEnabledWork
	EnabledWorkRefCount --;

	if(EnabledWorkRefCount == 0) if(Primary) Primary->NotifyNodeEnabledState();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryKeyDown(tjs_uint key, tjs_uint32 shift)
{
	if(FocusedLayer)
		FocusedLayer->FireKeyDown(key, shift);
	else
		if(Primary) Primary->DefaultKeyDown(key, shift);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryKeyUp(tjs_uint key, tjs_uint32 shift)
{
	if(FocusedLayer)
		FocusedLayer->FireKeyUp(key, shift);
	else
		if(Primary) Primary->DefaultKeyUp(key, shift);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryKeyPress(tjs_char key)
{
	if(FocusedLayer)
		FocusedLayer->FireKeyPress(key);
	else
		if(Primary) Primary->DefaultKeyPress(key);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::PrimaryMouseWheel(tjs_uint32 shift, tjs_int delta,
	tjs_int x, tjs_int y)
{
	if(FocusedLayer)
		FocusedLayer->FireMouseWheel(shift, delta, x, y);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::AddUpdateRegion(const tTVPComplexRect &rects)
{
	UpdateRegion.Or(rects);
	if(UpdateRegion.GetCount() > TVP_UPDATE_UNITE_LIMIT)
		UpdateRegion.Unite();
	NotifyWindowInvalidation();
}
//---------------------------------------------------------------------------
void tTVPLayerManager::AddUpdateRegion(const tTVPRect &rect)
{
	// the window is invalidated;
	UpdateRegion.Or(rect);
	NotifyWindowInvalidation();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::UpdateToDrawDevice()
{
	// drawdevice -> layer
	if(!Primary) return;
	Primary->CompleteForWindow(this);
}
//---------------------------------------------------------------------------
void tTVPLayerManager::NotifyUpdateRegionFixed()
{
	// called by primary layer, notifying final update region is fixed
//	Window->NotifyUpdateRegionFixed(UpdateRegion);
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::RequestInvalidation(const tTVPRect &r)
{
	// called by the owner window to notify window surface is invalidated by
	// the system or user.
	if(!Primary) return;

	tTVPRect ur;
	tTVPRect cr(0, 0, Primary->Rect.get_width(), Primary->Rect.get_height());

	if(TVPIntersectRect(&ur, r, cr))
	{
		AddUpdateRegion(ur);
	}
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::RecheckInputState()
{
	// To re-check current layer under current mouse position
	// and update hint, cursor type and process layer enter/leave.
	// This can be reasonably slow, about 1 sec interval.
	ForceMouseRecheck();
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPLayerManager::DumpLayerStructure()
{
	if(Primary) Primary->DumpStructure();
}
//---------------------------------------------------------------------------

bool tTVPDestTexture::CopyRect(tjs_int x, tjs_int y, const iTVPBaseBitmap *ref, tTVPRect refrect, tjs_int plane /*= (TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK)*/)
{
	if (HoldAlpha) {
		return tTVPBaseTexture::CopyRect(x, y, ref, refrect, TVP_BB_COPY_MAIN);
	} else {
		return tTVPBaseTexture::CopyRect(x, y, ref, refrect, plane);
	}
}
