package com.dolby.atmos;

import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/**
 * Плитка в шторке быстрых настроек: включает и выключает обработку Dolby
 * одним касанием, без открытия приложения.
 */
public class DolbyTileService extends TileService {

    @Override
    public void onStartListening() {
        super.onStartListening();
        updateTile();
    }

    @Override
    public void onClick() {
        super.onClick();
        Bridge b = new Bridge(getApplicationContext());
        boolean nowBypassed = b.isBypassed();
        b.setBypassed(!nowBypassed);
        Notify.update(this);
        updateTile();
    }

    private void updateTile() {
        Tile tile = getQsTile();
        if (tile == null) return;
        boolean on = !new Bridge(getApplicationContext()).isBypassed();
        tile.setState(on ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setLabel("Dolby Atmos");
        tile.updateTile();
    }
}
