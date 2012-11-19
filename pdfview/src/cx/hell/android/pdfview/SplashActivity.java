package cx.hell.android.pdfview;

import cx.hell.android.pdfview.R;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class SplashActivity extends Activity {

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.splash);
        Thread start_splash = new Thread(){
        	public void run(){
        		try{
        			sleep(4000);
        			finish();
        		}
        		catch(InterruptedException e){
        			e.printStackTrace();
        		}
        		finally{
        			//Intent start_main_activity = new Intent("cx.hell.android.pdfview.CHOOSEFILEACTIVITY");
        			Intent start_main_activity = new Intent("android.intent.action.CHOOSEFILEACTIVITY");
        			
        			startActivity(start_main_activity);
        		}
        	}
        };
        start_splash.start();
    }
}
