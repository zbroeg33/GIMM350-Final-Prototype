using System.Collections;
using System.Collections.Generic;
using UnityEngine.UI;
using UnityEngine.SceneManagement;
using UnityEngine;

public class menuScript : MonoBehaviour {


    public Button startText;
    public Button settingsText;
    public Button exitText;
	// Use this for initialization
	void Start ()
    {
        startText = startText.GetComponent<Button>();
        settingsText = settingsText.GetComponent<Button>();
        exitText = exitText.GetComponent<Button>();

	}

    public void StartLevel ()
    {
        SceneManager.LoadScene(1);
    }

    public void ExitGame()
    {
       // application.quit;
    }
	
	// Update is called once per frame
	void Update () {
		
	}
}
