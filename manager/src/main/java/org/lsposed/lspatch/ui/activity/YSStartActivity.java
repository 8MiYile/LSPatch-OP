package org.lsposed.lspatch.ui.activity;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.UserHandle;
import android.os.UserManager;
import android.system.Os;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

import androidx.annotation.Nullable;

import org.lsposed.lspatch.GlobalUserHandler;
import org.lsposed.lspatch.R;

import java.util.ArrayList;

public class YSStartActivity extends Activity {
    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        setContentView(R.layout.ys_start_container);


        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);

        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS ;
        getWindow().setAttributes(lp);



        View ysStartView = findViewById(R.id.ys_start_container_view);
        ysStartView.getBackground().setAlpha(0);
        new Handler(Looper.getMainLooper()).postDelayed(()-> setAlpha(ysStartView),500);

        new Handler(Looper.getMainLooper()).postDelayed(()->{
            if (!isDestroyed()){
                try {
                    GlobalUserHandler.mHandler = getSystemService(UserManager.class).getUserProfiles();
                }catch (Throwable ignored){ }
                if (GlobalUserHandler.mHandler == null){
                    GlobalUserHandler.mHandler = new ArrayList<>();
                    GlobalUserHandler.mHandler.add(UserHandle.getUserHandleForUid(Os.getuid()));
                }
            }
        },1000);

    }
    public void setAlpha(View ysStartView){
        runOnUiThread(()->{
            if (ysStartView.getBackground().getAlpha() < 252){
                ysStartView.getBackground().setAlpha(ysStartView.getBackground().getAlpha() + 3);
                new Handler(Looper.getMainLooper()).postDelayed(()->setAlpha(ysStartView), 10);
            }else {
                new Handler(Looper.getMainLooper()).postDelayed(()-> setAlpha2(ysStartView),1500);
            }
        });
    }
    public void setAlpha2(View ysStartView){
        runOnUiThread(()->{
            if (ysStartView.getBackground().getAlpha() > 3){
                ysStartView.getBackground().setAlpha(ysStartView.getBackground().getAlpha() - 3);
                new Handler(Looper.getMainLooper()).postDelayed(()->setAlpha2(ysStartView), 10);
            }else {
                Intent intent = new Intent(YSStartActivity.this, MainActivity.class);
                intent.putExtra("tag",YSStartActivity.class.getName());
                startActivity(intent);
                finish();
            }
        });
    }
}
